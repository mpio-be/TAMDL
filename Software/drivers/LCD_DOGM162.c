/***************************************************************************//**
 * @file
 * @brief	Routines for LCD Module EA DOGM162
 * @author	Ralf Gerhauser
 * @version	2018-03-02
 *
 * This module contains the low-level, i.e. the DOGM162 specific parts of the
 * display routines.  They are used by module DisplayMenu.c, but should never
 * be called directly by user code.
 *
 ****************************************************************************//*
Revision History:
2018-03-02,rage	Removed definition for "fields" because "lines" are enough.
		Implemented ability to define custom characters for the LCD.
		Added LCD_WriteLine() as a more efficient alternative to
		LCD_Printf(), because it doesn't use sprintf().
2016-04-05,rage	Made local variable <l_flgLCD_IsOn> of type "volatile".
2015-07-09,rage	IAR Compiler: Use vsprintf() instead vsiprintf().
2015-05-22,rage	Changed numeric defines to hexadecimal because the IAR compiler
		does not support binary constants.
2014-11-19,rage	Initial version.
*/

/*=============================== Header Files ===============================*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "em_device.h"
#include "em_assert.h"
#include "em_gpio.h"
#include "AlarmClock.h"
#include "LCD_DOGM162.h"

/*=============================== Definitions ================================*/

    /*!@name Hardware Configuration: Power Enable for the LCD Module. */
//@{
#define LCD_POWER_PORT		gpioPortD	//!< Port for power control
#define LCD_POWER_PIN		8		//!< Power Pin: 0=OFF, 1=ON
//@}

    /*!@name Hardware Configuration: Control lines to the LCD Module. */
//@{
#define LCD_CTRL_PORT		gpioPortF	//!< Port for control lines
#define LCD_CTRL_PIN_E		3		//!< Data Enable signal
#define LCD_CTRL_PIN_RW		4		//!< Read/Write signal
#define LCD_CTRL_PIN_RS		5		//!< Register Select signal
//@}

    /*!@name Hardware Configuration: Data bus to the LCD Module. */
//@{
#define LCD_DATA_PORT		gpioPortE	//!< Port for the data bus
#define LCD_DATA_MODE_OUT	0x44444444	//!< 8x @ref gpioModePushPull
#define LCD_DATA_MODE_IN	0x11111111	//!< 8x @ref gpioModeInput
#define LCD_DATA_MASK		(0xFF << 8)	//!< Data bus uses bit 15:8
//@}

    /*!@brief Timeout for WaitCtrlReady() is 1ms */
#define LCD_WAIT_READY_TIMEOUT	(RTC_COUNTS_PER_SEC / 1000)

    /*!@name I/O Macros providing access to the LCD Module. */
//@{
    //! Set level of the power enable pin
#define SET_LCD_POWER_PIN(level)  IO_Bit(GPIO->P[LCD_POWER_PORT].DOUT,	\
					 LCD_POWER_PIN) = (level)
    //! Configure the data bus to the LCD module for input, i.e. reading data.
#define SET_LCD_DATA_MODE_IN  GPIO->P[LCD_DATA_PORT].MODEH = LCD_DATA_MODE_IN
    //! Configure the data bus to the LCD module for output, i.e. writing data.
#define SET_LCD_DATA_MODE_OUT GPIO->P[LCD_DATA_PORT].MODEH = LCD_DATA_MODE_OUT
    //! Read data from the data bus of the LCD module.
#define READ_LCD_DATA()       (GPIO->P[LCD_DATA_PORT].DIN >> 8)
    /*!@brief Write data to the LCD module.  Writing directly to the GPIO data
     * out register is possible since port <b>E</b> is exclusively used for the
     * data bus (bit 7 to 0 are not routed to any pin for QFP64).
     */
#define WRITE_LCD_DATA(data)  (GPIO->P[LCD_DATA_PORT].DOUT = (data) << 8)
    //! Set level of control line "E"
#define SET_LCD_CTRL_PIN_E(level)  IO_Bit(GPIO->P[LCD_CTRL_PORT].DOUT,	\
					  LCD_CTRL_PIN_E) = (level)
    //! Set level of control line "RW"
#define SET_LCD_CTRL_PIN_RW(level) IO_Bit(GPIO->P[LCD_CTRL_PORT].DOUT,	\
					  LCD_CTRL_PIN_RW) = (level)
    //! Set level of control line "RS"
#define SET_LCD_CTRL_PIN_RS(level) IO_Bit(GPIO->P[LCD_CTRL_PORT].DOUT,	\
					  LCD_CTRL_PIN_RS) = (level)
//@}

    /*!@anchor commands @name Commands for the LCD Controller. */
//@{
#define LCD_CMD_CLEAR_DISPLAY	0x01	//!< Clear display, addr=0
#define LCD_CMD_RETURN_HOME	0x02	//!< Set addr=0, cursor home
#define LCD_CMD_ENTRY_MODE	0x04	//!< Cursor move direction right
#define LCD_CMD_ENTRY_MODE_ID	0x06	//!< Cursor move direction left
#define LCD_CMD_ENTRY_MODE_S	0x05	//!< Shift display left
#define LCD_CMD_DISPLAY_OFF	0x08	//!< Switch display OFF
#define LCD_CMD_DISPLAY_ON_D	0x0C	//!< Entire display ON
#define LCD_CMD_DISPLAY_ON_C	0x0A	//!< Cursor ON
#define LCD_CMD_DISPLAY_ON_B	0X09	//!< Cursor blinking ON
#define LCD_CMD_FCT_SET		0x20	//!< 4bit, 1 line, Instr-Tab 00
#define LCD_CMD_FCT_SET_DL	0X30	//!< Interface data is 8 bit
#define LCD_CMD_FCT_SET_N	0x28	//!< Select 2 lines
#define LCD_CMD_FCT_SET_DH	0x24	//!< Double Height
#define LCD_CMD_FCT_SET_IS2	0x22	//!< Instruction-Table 10
#define LCD_CMD_FCT_SET_IS1	0x21	//!< Instruction-Table 01
#define LCD_CMD_FCT_SET_IS0	0x20	//!< Instruction-Table 00
#define LCD_CMD_SET_DDRAM_ADDR	0x80	//!< Set DDRAM address
#define LCD_CMD_IS0_CD_SHIFT	0x10	//!< Set Cursor Shift (left)
#define LCD_CMD_IS0_CD_SHIFT_SC	0x18	//!< Set Display Shift (left)
#define LCD_CMD_IS0_CD_SHIFT_RL	0x14	//!< Cursor/Display Shift right
#define LCD_CMD_IS0_SET_CGRAM	0x40	//!< Set CGRAM address AC5:0
#define LCD_CMD_IS1_BIAS_SET	0x14	//!< BS=0: 1/5 bias
#define LCD_CMD_IS1_BIAS_SET_BL	0x1C	//!< BS=1: 1/4 bias
#define LCD_CMD_IS1_ICON_ADDR	0x40	//!< Set ICON address AC3:0
#define LCD_CMD_IS1_IBC		0x50	//!< ICON+Boost OFF, Contrast 0
#define LCD_CMD_IS1_IBC_ION	0x58	//!< ICON display ON
#define LCD_CMD_IS1_IBC_BON	0x54	//!< Set booster curcuit ON
#define LCD_CMD_IS1_IBC_C5	0x52	//!< Contrast bit 5
#define LCD_CMD_IS1_IBC_C4	0x51	//!< Contrast bit 4
#define LCD_CMD_IS1_CONTR	0x70	//!< Contrast
#define LCD_CMD_IS1_CONTR_C3	0x78	//!< Contrast bit 3
#define LCD_CMD_IS1_CONTR_C2	0x74	//!< Contrast bit 2
#define LCD_CMD_IS1_CONTR_C1	0x72	//!< Contrast bit 1
#define LCD_CMD_IS1_CONTR_C0	0x71	//!< Contrast bit 0
#define LCD_CMD_IS1_FOLLOW	0x60	//!< Follower Control (all 0)
#define LCD_CMD_IS1_FOLLOW_FON	0x68	//!< Follower Ctrl: FON=1
#define LCD_CMD_IS1_FOLLOW_RAB2	0x64	//!< Follower Ampl. Ratio: RAB2
#define LCD_CMD_IS1_FOLLOW_RAB1	0x62	//!< Follower Ampl. Ratio: RAB1
#define LCD_CMD_IS1_FOLLOW_RAB0	0x61	//!< Follower Ampl. Ratio: RAB0
#define LCD_CMD_IS2_DBL_HP	0x10	//!< Double Height Position UD=0
#define LCD_CMD_IS2_DBL_HP_UD	0x18	//!< Double Height Position UD=1
//@}

/*================================ Local Data ================================*/

    /*!@brief LCD Contrast value (0 to 63). */
static volatile int  l_Contrast = 25;

    /*!@brief Flag if LCD is on. */
static volatile bool l_flgLCD_IsOn;

    /* Custom Characters */
static const uint8_t l_CustChar[8][8] =
{
   {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},	// 0: blank
   {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},	// 1: blank
   {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},	// 2: blank
   {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},	// 3: blank
   {	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},	// 4: blank
   {	0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04, 0x00	},	// 5: UP Arrow
   {	0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04, 0x00	},	// 6: DOWN Arrow
   {	0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00, 0x00	}	// 7: RIGHT Arrow
};

/*=========================== Forward Declarations ===========================*/

static uint8_t BusyRead (void);
static bool WaitCtrlReady (void);
static void CmdWrite (uint8_t cmd);
// static uint8_t DataRead (void);
static void DataWrite (uint8_t data);


/***************************************************************************//**
 *
 * @brief	Initialize LCD
 *
 * This routine initializes the board-specific hardware (GPIOs) and the LCD
 * controller itself.
 *
 ******************************************************************************/
void LCD_Init (void)
{
    /* Power the LCD Module On and initialize it */
    LCD_PowerOn();
}


/***************************************************************************//**
 *
 * @brief	Power LCD On
 *
 * This routine powers the LCD on and initializes the related hardware.
 *
 ******************************************************************************/
void LCD_PowerOn (void)
{
int	ch, i;

    /* Configure control lines E, RW, and RS as output */
    GPIO_PinModeSet (LCD_CTRL_PORT, LCD_CTRL_PIN_E, gpioModePushPull, 0);
    GPIO_PinModeSet (LCD_CTRL_PORT, LCD_CTRL_PIN_RW, gpioModePushPull, 0);
    GPIO_PinModeSet (LCD_CTRL_PORT, LCD_CTRL_PIN_RS, gpioModePushPull, 0);

    /* Set data bus to 0 for default */
    GPIO->P[LCD_DATA_PORT].DOUTCLR = LCD_DATA_MASK;

    /* Configure Power Enable Pin for LCD Module, switch it ON */
    GPIO_PinModeSet (LCD_POWER_PORT, LCD_POWER_PIN, gpioModePushPull, 1);

    /* Wait until LCD is powered up and ready */
    msDelay (100);

    /* Set 8bit data width, 2 lines, and instruction table 1 */
    CmdWrite (LCD_CMD_FCT_SET_DL|LCD_CMD_FCT_SET_N|LCD_CMD_FCT_SET_IS1);

    /* Instruction table 1: BIAS Set BS=0: 1/5 bias for a 2 line LCD */
    CmdWrite (LCD_CMD_IS1_BIAS_SET);

    /* Instruction table 1: booster ON, contrast bit C5:4 */
    CmdWrite (LCD_CMD_IS1_IBC_BON |(l_Contrast >> 4));

    /* Instruction table 1: Follower Ctrl FON=1, Amplifier Ratio = 5 */
    CmdWrite (LCD_CMD_IS1_FOLLOW_FON|LCD_CMD_IS1_FOLLOW_RAB2
				    |LCD_CMD_IS1_FOLLOW_RAB0);

    /* Set LCD Contrast bit C3:0 */
    CmdWrite (LCD_CMD_IS1_CONTR |(l_Contrast & 0x0F));

    /* Select instruction table 0 */
    CmdWrite (LCD_CMD_FCT_SET_DL|LCD_CMD_FCT_SET_N|LCD_CMD_FCT_SET_IS0);

    /* Load custom characters into CGRAM */
    for (ch = 0;  ch < 8;  ch++)
    {
	CmdWrite (LCD_CMD_IS0_SET_CGRAM | (ch << 3));
	for (i = 0;  i < 8;  i++)
	    DataWrite(l_CustChar[ch][i]);
    }

    /* Switch display ON, cursor OFF and no blinking */
    CmdWrite (LCD_CMD_DISPLAY_ON_D);

    /* Clear display, set cursor home */
    CmdWrite (LCD_CMD_CLEAR_DISPLAY);

    /* Set cursor to autoincrement mode */
    CmdWrite (LCD_CMD_ENTRY_MODE_ID);

    /* LCD is now ON */
    l_flgLCD_IsOn = true;
}


/***************************************************************************//**
 *
 * @brief	Power LCD Off
 *
 * This routine powers the LCD off.
 *
 ******************************************************************************/
void LCD_PowerOff (void)
{
    /* LCD will be switched OFF */
    l_flgLCD_IsOn = false;

    /* Set Power Enable Pin to OFF */
    SET_LCD_POWER_PIN(0);

    /*
     * Set all other signals also to GND, otherwise these will provide enough
     * power for the LCD to be still active!
     */
    SET_LCD_DATA_MODE_OUT;	// data bus output mode
    SET_LCD_CTRL_PIN_RW(0);	// set RW pin to 0
    SET_LCD_CTRL_PIN_RS(0);	// set RS pin to 0
    WRITE_LCD_DATA (0x00);	// set data bus to 0x00
    SET_LCD_CTRL_PIN_E (0);	// set E pin to 0
}


/***************************************************************************//**
 *
 * @brief	Print string to LCD
 *
 * This routine is used to print text to the specified field on the LC-Display.
 *
 * @param[in] lineNum
 *	The line number where to display the text.  Must be 1 or 2.
 *
 * @param[in] frmt
 *	Format string of the text to print - same as for printf().
 *
 ******************************************************************************/
void LCD_Printf (int lineNum, const char *frmt, ...)
{
va_list	 args;


    va_start (args, frmt);
    LCD_vPrintf (lineNum, frmt, args);
    va_end (args);
}


/***************************************************************************//**
 *
 * @brief	Print string with va_list to LCD
 *
 * This routine is identical to LCD_Printf(), except parameter @p args is a
 * variable argument list.
 *
 *
 * @param[in] lineNum
 *	The line number where to display the text.  Must be 1 or 2.
 *
 * @param[in] frmt
 *	Format string of the text to print - same as for printf().
 *
 * @param[in] args
 *	Variable argument list.
 *
 ******************************************************************************/
void LCD_vPrintf (int lineNum, const char *frmt, va_list args)
{
char	 buffer[40];


    /* Immediately return if LCD is OFF */
    if (! l_flgLCD_IsOn)
	return;

    if (strlen(frmt) > (sizeof(buffer) - 10))
    {
	EFM_ASSERT(0);
	return;
    }

    vsprintf (buffer, frmt, args);

    LCD_WriteLine (lineNum, buffer);
}


/***************************************************************************//**
 *
 * @brief	Write Line to LCD
 *
 * This routine writes the given string buffer to the specified line.
 *
 * @param[in] lineNum
 *	The line number where to display the text.  Must be 1 or 2.
 *
 * @param[in] buffer
 *	String buffer with text.
 *
 ******************************************************************************/
void LCD_WriteLine (int lineNum, char *buffer)
{
int	 len;


    /* Parameter check */
    if (lineNum < 1  ||  lineNum > 2)
    {
	EFM_ASSERT(false);
	return;
    }

    /* Get string length */
    len = strlen (buffer);

    /* If string is too long, truncate it */
    if (len > LCD_DIMENSION_X)
	buffer[LCD_DIMENSION_X] = EOS;

    /* If string is shorter than field width, add spaces */
    while (len < LCD_DIMENSION_X)
	buffer[len++] = ' ';

    buffer[LCD_DIMENSION_X] = EOS;

    /* Set LCD cursor to the beginning of the line */
    LCD_GotoXY (0, lineNum - 1);

    /* Output string to LCD */
    LCD_Puts (buffer);
}


/***************************************************************************//**
 *
 * @brief	Put string to LCD
 *
 * This routine puts the specified string to the LC-Display.
 *
 * @param[in] pStr
 *	String to output on the LCD at the actual cursor position.
 *
 ******************************************************************************/
void LCD_Puts (char *pStr)
{
    while (*pStr != EOS)
	LCD_Putc (*pStr++);
}


/***************************************************************************//**
 *
 * @brief	Put character to LCD
 *
 * This routine puts the specified character to the LC-Display.
 *
 * @param[in] c
 *	Character to output on the LCD at the actual cursor position.
 *
 ******************************************************************************/
void LCD_Putc (char c)
{
    /* Write character to LCD data bus */
    if (l_flgLCD_IsOn)
	DataWrite (c);
}


/***************************************************************************//**
 *
 * @brief	Move cursor on X-/Y-Position
 *
 * This routine moves the cursor the the specified position on the LC-Display.
 * Coordinates 0,0 represent the upper left corner of the display.
 *
 * @param[in] x
 *	X-Position to move cursor to.
 *
 * @param[in] y
 *	Y-Position to move cursor to.
 *
 ******************************************************************************/
void LCD_GotoXY (uint8_t x, uint8_t y)
{
uint8_t addr;


    /* Immediately return if LCD is OFF */
    if (! l_flgLCD_IsOn)
	return;

    EFM_ASSERT (x < LCD_DIMENSION_X  &&  y < LCD_DIMENSION_Y);

    addr = (y * 0x40) + x;

    CmdWrite (LCD_CMD_SET_DDRAM_ADDR | addr);
}


/***************************************************************************//**
 *
 * @brief	Read Busy Flag and Address
 *
 * This routine reads the busy flag and current value of the internal address
 * counter of the LC-Display.
 *
 * @return
 *	Current status: busy flag in bit [7] and address counter in bits [6:0].
 *
 ******************************************************************************/
static uint8_t BusyRead (void)
{
uint8_t status;

    SET_LCD_DATA_MODE_IN;	// input
    SET_LCD_CTRL_PIN_RW(1);	// read
    SET_LCD_CTRL_PIN_RS(0);	// register
    SET_LCD_CTRL_PIN_E (1);	// enable LCD output

    DelayTick();
    status = READ_LCD_DATA();	// read busy flag

    SET_LCD_CTRL_PIN_E (0);	// disable LCD output

    return status;
}


/***************************************************************************//**
 *
 * @brief	Wait until the LCD Controller is ready
 *
 * This routine reads the current status of the LCD controller and checks its
 * busy flag.  It waits as long as the busy flag is 1, i.e. the controller
 * is not ready to receive new commands or data due to internal activity, or
 * a timeout is reached.
 *
 * @return
 *	Status: false if LCD is ready now, true in case of timeout
 *
 ******************************************************************************/
static bool WaitCtrlReady (void)
{
int	i;

    for (i = 0;  i < LCD_WAIT_READY_TIMEOUT;  i++)
    {
	if ((BusyRead() & (1 << 7)) == false)
	    return false;

	DelayTick();	// delay for 30us
    }

    return true;	// timeout
}


/***************************************************************************//**
 *
 * @brief	Write Command to LCD Controller
 *
 * This routine waits until the LCD controller is ready and then writes
 * the specified command to it.  For a list of available commands, see
 * @ref commands "Commands for the LCD Controller".
 *
 * @param[in] cmd
 *	Command to write to the LCD controller.
 *
 ******************************************************************************/
static void CmdWrite (uint8_t cmd)
{
    /* Check if LCD controller is ready to receive a new command */
    if (WaitCtrlReady())
	return;			// timeout - abort

    SET_LCD_DATA_MODE_OUT;	// output
    SET_LCD_CTRL_PIN_RW(0);	// write
    SET_LCD_CTRL_PIN_RS(0);	// register

    WRITE_LCD_DATA (cmd);

    SET_LCD_CTRL_PIN_E (1);	// enable data valid
    DelayTick();
    SET_LCD_CTRL_PIN_E (0);	// disable data valid
}


#if 0
/***************************************************************************//**
 *
 * @brief	Read Data from LCD Memory
 *
 * This routine reads the data byte, i.e. character code, from the current
 * address of the internal memory of the LCD controller.  Use the
 * command @ref LCD_CMD_SET_DDRAM_ADDR to change the value of the internal
 * address pointer.
 *
 * @return
 *	The byte (character) as read from the internal memory.
 *
 ******************************************************************************/
static uint8_t DataRead (void)
{
uint8_t data;

    SET_LCD_DATA_MODE_IN;	// input
    SET_LCD_CTRL_PIN_RW(1);	// read
    SET_LCD_CTRL_PIN_RS(1);	// data bus
    SET_LCD_CTRL_PIN_E (1);	// enable LCD output

    DelayTick();
    data = READ_LCD_DATA();	// read data bus

    SET_LCD_CTRL_PIN_E (0);	// disable LCD output

    return data;
}
#endif


/***************************************************************************//**
 *
 * @brief	Write Data to LCD Memory
 *
 * This routine writes the specified data byte, i.e. character code, to the
 * current address of the internal memory of the LCD controller.  Use the
 * command @ref LCD_CMD_SET_DDRAM_ADDR to change the value of the internal
 * address pointer.
 *
 * @param[in] data
 *	Data to write to the internal memory of the LCD controller.
 *
 ******************************************************************************/
static void DataWrite (uint8_t data)
{
    /* Check if LCD controller is ready to receive new data */
    if (WaitCtrlReady())
	return;			// timeout - abort

    SET_LCD_DATA_MODE_OUT;	// output
    SET_LCD_CTRL_PIN_RW(0);	// write
    SET_LCD_CTRL_PIN_RS(1);	// data bus

    WRITE_LCD_DATA (data);

    SET_LCD_CTRL_PIN_E (1);	// enable data valid
    DelayTick();
    SET_LCD_CTRL_PIN_E (0);	// disable data valid
}
