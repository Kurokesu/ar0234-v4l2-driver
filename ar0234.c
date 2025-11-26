// SPDX-License-Identifier: GPL-2.0
/*
* A V4L2 driver for OnSemi AR0234 cameras.
* Copyright (C) 2021, Raspberry Pi (Trading) Ltd
*
* Based on Sony imx219 camera driver
* Copyright (C) 2019, Raspberry Pi (Trading) Ltd
*
*/

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/unaligned.h>

#define AR0234_REG_VALUE_08BIT		1
#define AR0234_REG_VALUE_16BIT		2

/* Chip ID */
#define AR0234_REG_CHIP_ID		0x3000
#define AR0234_CHIP_ID			0x0a56
#define AR0234_CHIP_ID_MONO		0x1a56

#define AR0234_REG_RESET          0x301A
/* Bit 0 is reset */
/* Bit 2 is stream on/off */
#define AR0234_REG_RESET_RESET       0x00D9
#define AR0234_REG_RESET_STREAM_OFF  0x2058
#define AR0234_REG_RESET_STREAM_ON   0x205C

/* External clock frequency is 24.0M */
#define AR0234_FREQ_EXTCLK		24000000

#define AR0234_FREQ_PIXCLK_2LANE	45000000
#define AR0234_FREQ_PIXCLK_4LANE	90000000

#define AR0234_FREQ_LINK_10BIT	450000000

#define LINE_LENGTH_PCK			0x300C

/* V_TIMING internal */
#define AR0234_REG_VTS			0x300a
#define AR0234_VTS_30FPS		0x04c4
#define AR0234_VTS_60FPS		0x0dc6 //fixme
#define AR0234_VTS_MAX			0xffff

#define AR0234_VBLANK_MIN		16

/*Frame Length Line*/
#define AR0234_FLL_MIN			0x08a6
#define AR0234_FLL_MAX			0xffff
#define AR0234_FLL_STEP			1
#define AR0234_FLL_DEFAULT		0x0c98

/* HBLANK control - read only */
#define AR0234_PPL_DEFAULT		2448

/* Exposure control */
#define AR0234_REG_EXPOSURE_COARSE	0x3012
#define AR0234_REG_EXPOSURE_FINE	0x3014
#define AR0234_EXPOSURE_MIN		4
#define AR0234_EXPOSURE_STEP		1
#define AR0234_EXPOSURE_DEFAULT		0x640
#define AR0234_EXPOSURE_MAX		65535

/* Analog gain control */
#define AR0234_REG_ANALOG_GAIN		0x3060
#define AR0234_ANA_GAIN_MIN		0
#define AR0234_ANA_GAIN_MAX		232
#define AR0234_ANA_GAIN_STEP		1
#define AR0234_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define AR0234_REG_DIGITAL_GAIN		0x305e
#define AR0234_DGTL_GAIN_MIN		0x0100
#define AR0234_DGTL_GAIN_MAX		0x0fff
#define AR0234_DGTL_GAIN_DEFAULT	0x0100
#define AR0234_DGTL_GAIN_STEP		1

#define AR0234_REG_ORIENTATION		0x3040
#define AR0234_REG_ORIENTATION_HFLIP	BIT(14)
#define AR0234_REG_ORIENTATION_VFLIP	BIT(15)

#define AR0234_REG_OUTPUT_DEPTH		0x31AC

/* Test Pattern Control */
#define AR0234_REG_TEST_PATTERN		0x0600
#define AR0234_TEST_PATTERN_DISABLE	0
#define AR0234_TEST_PATTERN_SOLID_COLOR	1
#define AR0234_TEST_PATTERN_COLOR_BARS	2
#define AR0234_TEST_PATTERN_GREY_COLOR	3
#define AR0234_TEST_PATTERN_PN9		4

/* Test pattern colour components */
#define AR0234_REG_TESTP_RED		0x0602
#define AR0234_REG_TESTP_GREENR		0x0604
#define AR0234_REG_TESTP_BLUE		0x0606
#define AR0234_REG_TESTP_GREENB		0x0608
#define AR0234_TESTP_COLOUR_MIN		0
#define AR0234_TESTP_COLOUR_MAX		0x03ff
#define AR0234_TESTP_COLOUR_STEP	1
#define AR0234_TESTP_RED_DEFAULT	AR0234_TESTP_COLOUR_MAX
#define AR0234_TESTP_GREENR_DEFAULT	0
#define AR0234_TESTP_BLUE_DEFAULT	0
#define AR0234_TESTP_GREENB_DEFAULT	0

/* Helper macro for declaring ar0234 reg sequence */
#define AR0234_REG_SEQ(_reg_array)                                      \
	{                                                               \
		.regs = (_reg_array), .amount = ARRAY_SIZE(_reg_array), \
	}

enum pad_types {
	IMAGE_PAD,
	METADATA_PAD,
	NUM_PADS
};

/* AR0234 native and active pixel array size. */
#define AR0234_NATIVE_WIDTH		1484U
#define AR0234_NATIVE_HEIGHT		856U
#define AR0234_PIXEL_ARRAY_LEFT		6U
#define AR0234_PIXEL_ARRAY_TOP		10U
#define AR0234_PIXEL_ARRAY_WIDTH	1920U
#define AR0234_PIXEL_ARRAY_HEIGHT	1200U

/* Embedded metadata stream structure */
// Padding every 4 bytes
#define AR0234_MD_PADDING_BYTES (AR0234_PIXEL_ARRAY_WIDTH / 4)
#define AR0234_EMBEDDED_LINE_WIDTH                                             \
  (AR0234_PIXEL_ARRAY_WIDTH + AR0234_MD_PADDING_BYTES)
#define AR0234_NUM_EMBEDDED_LINES 2

struct ar0234_reg {
	u16 address;
	u16 val;
};

struct ar0234_reg_sequence {
	unsigned int amount;
	const struct ar0234_reg *regs;
};

struct ar0234_timing {
	unsigned int line_length_pck;
	unsigned int frame_length_lines_min;
};

enum ar0234_lane_mode_id {
	AR0234_LANE_MODE_ID_2 = 0,
	AR0234_LANE_MODE_ID_4,
	AR0234_LANE_MODE_ID_AMOUNT,
};

enum ar0234_bit_depth_id {
	AR0234_BIT_DEPTH_ID_8BIT = 0,
	AR0234_BIT_DEPTH_ID_10BIT,
	AR0234_BIT_DEPTH_ID_AMOUNT,
};

/* Mode : resolution and related config&values */
struct ar0234_format {
	/* Frame width */
	unsigned int width;
	/* Frame height */
	unsigned int height;

	/* Analog crop rectangle. */
	struct v4l2_rect crop;

	struct ar0234_timing timing[AR0234_LANE_MODE_ID_AMOUNT]
				   [AR0234_BIT_DEPTH_ID_AMOUNT];

	struct ar0234_reg_sequence reg_sequence;
};

struct ar0234_mode {
	struct ar0234_format const *format;
	enum ar0234_bit_depth_id bit_depth;
};

#define VT_PIX_CLK_DIV     0x302A
#define VT_SYS_CLK_DIV     0x302C
#define PRE_PLL_CLK_DIV    0x302E
#define PLL_MULTIPLIER     0x3030
#define OP_PIX_CLK_DIV     0x3036
#define OP_SYS_CLK_DIV     0x3038
#define DIGITAL_TEST       0x30B0

#define DELAY 0xffff	/* Delay for specified number of ms */

static const struct ar0234_reg ar0234_pll_config_24_720_8bit[] = {
	{ 0x302A, 0x0008 },	// VT_PIX_CLK_DIV
	{ 0x302C, 0x0001 },	// VT_SYS_CLK_DIV
	{ 0x302E, 0x0001 },	// PRE_PLL_CLK_DIV
	{ 0x3030, 0x001E },	// PLL_MULTIPLIER
	{ 0x3036, 0x0008 },	// OP_PIX_CLK_DIV
	{ 0x3038, 0x0002 },	// OP_SYS_CLK_DIV
};

static const struct ar0234_reg ar0234_pll_config_24_900_10bit[] = {
	{ 0x302A, 0x0005 },	// VT_PIX_CLK_DIV
	{ 0x302C, 0x0001 },	// VT_SYS_CLK_DIV
	{ 0x302E, 0x0008 },	// PRE_PLL_CLK_DIV
	{ 0x3030, 0x0096 },	// PLL_MULTIPLIER
	{ 0x3036, 0x000A },	// OP_PIX_CLK_DIV
	{ 0x3038, 0x0001 },	// OP_SYS_CLK_DIV
};

static const struct ar0234_reg ar0234_mipi_config_24_720_8bit[] = {
	{ 0x31B0, 0x0080 },	// FRAME_PREAMBLE
	{ 0x31B2, 0x005C },	// LINE_PREAMBLE
	{ 0x31B4, 0x5248 },	// MIPI_TIMING_0
	{ 0x31B6, 0x4258 },	// MIPI_TIMING_1
	{ 0x31B8, 0x904C },	// MIPI_TIMING_2
	{ 0x31BA, 0x028B },	// MIPI_TIMING_3
	{ 0x31BC, 0x0D89 },	// MIPI_TIMING_4
	{ 0x3354, 0x002A }, // MIPI_CNTRL
};

static const struct ar0234_reg ar0234_mipi_config_24_900_10bit[] = {
	{ 0x31B0, 0X0082 },	// FRAME_PREAMBLE
	{ 0x31B2, 0X005C },	// LINE_PREAMBLE
	{ 0x31B4, 0X4248 },	// MIPI_TIMING_0
	{ 0x31B6, 0X4258 },	// MIPI_TIMING_1
	{ 0x31B8, 0X904B },	// MIPI_TIMING_2
	{ 0x31BA, 0X030B },	// MIPI_TIMING_3
	{ 0x31BC, 0X0D89 },	// MIPI_TIMING_4
	{ 0x3354, 0x002B }, // MIPI_CNTRL
};

static const struct ar0234_reg ar0234_reset[] = {
	//[Reset]
	{DELAY, 20 },
	{AR0234_REG_RESET, AR0234_REG_RESET_RESET}, //RESET_REGISTER
	{DELAY, 200 },
	{ 0x301A, 0x2058 },	// RESET_REGISTER
};

static const struct ar0234_reg common_init[] = {
	{ 0x30B0, 0x0028 },	// DIGITAL_TEST
	{ 0x306E, 0x9010 },	// DATAPATH_SELECT
	{ 0x3082, 0x0003 },	// OPERATION_MODE_CTRL
	{ 0x3040, 0x0000 },	// READ_MODE
	{ 0x31D0, 0x0000 },	// COMPANDING
	{ 0x3088, 0x8050 },	// SEQ_CTRL_PORT
//	{ 0x3086, 0x9237 },	// SEQ_DATA_PORT
	{ 0x3096, 0x0280 },	// RESERVED_MFR_3096
	{ 0x31E0, 0x0003 },	// PIX_DEF_ID
	{ 0x3F4C, 0x121F },	// RESERVED_MFR_3F4C
	{ 0x3F4E, 0x121F },	// RESERVED_MFR_3F4E
	{ 0x3F50, 0x0B81 },	// RESERVED_MFR_3F50
	{ 0x3088, 0x81BA },	// SEQ_CTRL_PORT
	{ 0x3086, 0x3D02 },	// SEQ_DATA_PORT
	{ 0x3ED2, 0xFA96 },	// RESERVED_MFR_3ED2
	{ 0x3180, 0x824F },	// DELTA_DK_CONTROL
	{ 0x3ECC, 0x0D42 },	// RESERVED_MFR_3ECC
	{ 0x3ECC, 0x0D42 },	// RESERVED_MFR_3ECC
	{ 0x30F0, 0x2283 },	// RESERVED_MFR_30F0
	{ 0x3102, 0x5000 },	// AE_LUMA_TARGET_REG
	{ 0x30B4, 0x0011 }, // TEMPSENS_CTRL_REG
	{ 0x30BA, 0x7626 },	// RESERVED_MFR_30BA
	{ 0x301A, 0x205C },	// RESET_REGISTER
	{ 0x3064, 0x1982 },	// EMBEDDED DATA
};

static const struct ar0234_reg ar0234_1920x1200_config[] = {
	// [1920x1200 60fps]
	/* Cropping / output size */
	{ 0x3002, 0x0008 },	// Y_ADDR_START
	{ 0x3004, 0x0008 },	// X_ADDR_START
	{ 0x3006, 0x04b7 },	// Y_ADDR_END
	{ 0x3008, 0x0787 },	// X_ADDR_END
	{ LINE_LENGTH_PCK, 0x0264 },	// LINE_LENGTH_PCK
	{ 0x30A2, 0x0001 },	// X_ODD_INC
	{ 0x30A6, 0x0001 },	// Y_ODD_INC

//	{0x3028, 0x0010}, //ROW_SPEED
};

static const struct ar0234_reg ar0234_1280x800_config[] = {
	// [1280x800 60fps]
	{ 0x3002, 0x00d0 },	// Y_ADDR_START
	{ 0x3004, 0x0148 },	// X_ADDR_START
	{ 0x3006, 0x03ef },	// Y_ADDR_END
	{ 0x3008, 0x0647 },	// X_ADDR_END
	{ LINE_LENGTH_PCK, 0x0264 },	// LINE_LENGTH_PCK - CHECKME
	{ 0x30A2, 0x0001 },	// X_ODD_INC
	{ 0x30A6, 0x0001 },	// Y_ODD_INC
};

static const char * const ar0234_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

static const int ar0234_test_pattern_val[] = {
	AR0234_TEST_PATTERN_DISABLE,
	AR0234_TEST_PATTERN_COLOR_BARS,
	AR0234_TEST_PATTERN_SOLID_COLOR,
	AR0234_TEST_PATTERN_GREY_COLOR,
	AR0234_TEST_PATTERN_PN9,
};

/* regulator supplies */
static const char * const ar0234_supply_name[] = {
	/* Supplies can be enabled in any order */
	"vana",  /* Analog (2.8V) supply */
	"vdig",  /* Digital Core (1.8V) supply */
	"vddl",  /* IF (1.2V) supply */
};

#define AR0234_NUM_SUPPLIES ARRAY_SIZE(ar0234_supply_name)

#define AR0234_XCLR_MIN_DELAY_US	6200
#define AR0234_XCLR_DELAY_RANGE_US	1000

static const u32 bayer_codes[] = {
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGRBG8_1X8,
};

static const u32 mono_codes[] = {
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_Y8_1X8,
};

static const s64 link_freq[] = {
	AR0234_FREQ_LINK_10BIT,
};

/*
* There is an inherent assumption that there will be the same number of codes
* for the Bayer and monochrome sensors
*/
#define NUM_CODES ARRAY_SIZE(bayer_codes)

/* Format configs */
static const struct ar0234_format ar0234_formats_24_900[] = {
	{
		/* 1280x800 60fps mode */
		.width = 1920,
		.height = 1200,
		.crop = {
			.left = AR0234_PIXEL_ARRAY_LEFT,
			.top = AR0234_PIXEL_ARRAY_TOP,
			.width = 1920,
			.height = 1200
		},
		.timing = {
			[AR0234_LANE_MODE_ID_2][AR0234_BIT_DEPTH_ID_8BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
			[AR0234_LANE_MODE_ID_2][AR0234_BIT_DEPTH_ID_10BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
			[AR0234_LANE_MODE_ID_4][AR0234_BIT_DEPTH_ID_8BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
			[AR0234_LANE_MODE_ID_4][AR0234_BIT_DEPTH_ID_10BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
		},
		.reg_sequence = AR0234_REG_SEQ(ar0234_1920x1200_config),
	},
	{
		/* Cropped 1280x720 30fps mode */
		.width = 1280,
		.height = 800,
		.crop = {
			.left = 320,
			.top = 200,
			.width = 1280,
			.height = 800
		},
		.timing = {
			[AR0234_LANE_MODE_ID_2][AR0234_BIT_DEPTH_ID_8BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
			[AR0234_LANE_MODE_ID_2][AR0234_BIT_DEPTH_ID_10BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
			[AR0234_LANE_MODE_ID_4][AR0234_BIT_DEPTH_ID_8BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
			[AR0234_LANE_MODE_ID_4][AR0234_BIT_DEPTH_ID_10BIT] = {
				.line_length_pck = 612,
				.frame_length_lines_min = AR0234_VTS_30FPS,
			},
		},
		.reg_sequence = AR0234_REG_SEQ(ar0234_1280x800_config),
	},
};

struct ar0234_pll_config {
	s64 freq_link;
	u64 freq_extclk;
	u32 freq_pixclk[AR0234_LANE_MODE_ID_AMOUNT];

	unsigned int formats_amount;
	struct ar0234_format const *formats;

	struct ar0234_reg_sequence regs_pll;
	struct ar0234_reg_sequence regs_mipi[AR0234_BIT_DEPTH_ID_AMOUNT];
};

static const struct ar0234_pll_config ar0234_pll_config = {
	.freq_link = AR0234_FREQ_LINK_10BIT,
	.freq_extclk = AR0234_FREQ_EXTCLK,
	.freq_pixclk = {
		[AR0234_LANE_MODE_ID_2] = AR0234_FREQ_PIXCLK_2LANE,
		[AR0234_LANE_MODE_ID_4] = AR0234_FREQ_PIXCLK_4LANE,
	},
	.formats = ar0234_formats_24_900,
	.formats_amount = ARRAY_SIZE(ar0234_formats_24_900),
	.regs_pll = AR0234_REG_SEQ(ar0234_pll_config_24_900_10bit),
	.regs_mipi = {
		[AR0234_BIT_DEPTH_ID_8BIT] = AR0234_REG_SEQ(ar0234_mipi_config_24_720_8bit),
		[AR0234_BIT_DEPTH_ID_10BIT] = AR0234_REG_SEQ(ar0234_mipi_config_24_900_10bit),
	},
};

struct ar0234_hw_config {
	struct clk *extclk;
	struct regulator_bulk_data supplies[AR0234_NUM_SUPPLIES];
	struct gpio_desc *gpio_reset;
	unsigned int num_data_lanes;
	enum ar0234_lane_mode_id lane_mode;
};

struct ar0234 {
	struct device *dev;
	struct ar0234_hw_config hw_config;

	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	struct v4l2_mbus_framefmt fmt;

	bool monochrome;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	struct ar0234_mode mode;

	/*
	* Mutex for serialized access:
	* Protect sensor module set pad format and start/stop streaming safely.
	*/
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

static inline struct ar0234 *to_ar0234(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct ar0234, sd);
}

/* Read registers up to 2 at a time */
static int ar0234_read_reg(struct ar0234 *ar0234, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		pr_err("i2c_transfer returned %d\n", ret);
		return -EIO;
	}

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/* Write registers up to 2 at a time */
static int ar0234_write_reg(struct ar0234 *ar0234, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int ar0234_write_regs(struct ar0234 *ar0234,
				const struct ar0234_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		if (regs[i].address == DELAY) {
			usleep_range(regs[i].val * 1000,
					(regs[i].val + 1) * 1000);
			continue;
		}

		ret = ar0234_write_reg(ar0234, regs[i].address, 2, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
						"Failed to write reg 0x%4.4x. error = %d\n",
						regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static const u32 *ar0234_get_codes(struct ar0234 *ar0234)
{
	if (ar0234->monochrome)
		return mono_codes;
	else
		return bayer_codes;
}

/* Get bayer order based on flip setting. */
static u32 ar0234_get_format_code(struct ar0234 *ar0234, u32 code)
{
	const u32 *codes = ar0234_get_codes(ar0234);
	unsigned int i;

	lockdep_assert_held(&ar0234->mutex);

	for (i = 0; i < NUM_CODES; i++)
		if (codes[i] == code)
			break;

	if (i >= NUM_CODES)
		i = 0;

	return codes[i];
}

static void ar0234_set_default_format(struct ar0234 *ar0234)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = &ar0234->fmt;
	fmt->code = ar0234_get_codes(ar0234)[0];

	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							fmt->colorspace,
							fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = ar0234_formats_24_900[0].width;
	fmt->height = ar0234_formats_24_900[0].height;
	fmt->field = V4L2_FIELD_NONE;

	ar0234->mode.format = &ar0234_formats_24_900[0];
	ar0234->mode.bit_depth = AR0234_BIT_DEPTH_ID_10BIT;
}

static int ar0234_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;

	mutex_lock(&ar0234->mutex);

	/* Initialize try_fmt for the image pad */
	try_fmt_img->width = ar0234_formats_24_900[0].width;
	try_fmt_img->height = ar0234_formats_24_900[0].height;
	try_fmt_img->code = ar0234_get_format_code(ar0234, 0);
	try_fmt_img->field = V4L2_FIELD_NONE;

	/* Initialize try_fmt for the embedded metadata pad */
	try_fmt_meta->width = AR0234_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = AR0234_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	/* Initialize try_crop rectangle. */
	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->top = AR0234_PIXEL_ARRAY_TOP;
	try_crop->left = AR0234_PIXEL_ARRAY_LEFT;
	try_crop->width = AR0234_PIXEL_ARRAY_WIDTH;
	try_crop->height = AR0234_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&ar0234->mutex);

	return 0;
}

static void ar0234_adjust_exposure_range(struct ar0234 *ar0234)
{
	int exposure_max, exposure_def;

	/* Honour the VBLANK limits when setting exposure. */
	exposure_max = ar0234->mode.format->height + ar0234->vblank->val -
		       AR0234_EXPOSURE_MIN;
	exposure_def = min(exposure_max, ar0234->exposure->val);
	__v4l2_ctrl_modify_range(ar0234->exposure, ar0234->exposure->minimum,
				 exposure_max, ar0234->exposure->step,
				 exposure_def);
}

static int ar0234_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ar0234 *ar0234 =
		container_of(ctrl->handler, struct ar0234, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	int ret;

	if (ctrl->id == V4L2_CID_VBLANK)
		ar0234_adjust_exposure_range(ar0234);

	/*
	* Applying V4L2 control value only happens
	* when power is up for streaming
	*/
	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ar0234_write_reg(ar0234, AR0234_REG_ANALOG_GAIN,
					AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ar0234_write_reg(ar0234, AR0234_REG_EXPOSURE_COARSE,
					AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ar0234_write_reg(ar0234, AR0234_REG_DIGITAL_GAIN,
				       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret =  0;//ar0234_write_reg(ar0234, AR0234_REG_TEST_PATTERN,
			//	       AR0234_REG_VALUE_16BIT,
			//	       ar0234_test_pattern_val[ctrl->val]);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	{
		u32 reg;

		ret = ar0234_read_reg(ar0234, AR0234_REG_ORIENTATION,
					AR0234_REG_VALUE_16BIT, &reg);
		if (ret)
			break;

		reg &= ~(AR0234_REG_ORIENTATION_HFLIP |
			AR0234_REG_ORIENTATION_VFLIP);
		if (ar0234->hflip->val)
			reg |= AR0234_REG_ORIENTATION_HFLIP;
		if (ar0234->vflip->val)
			reg |= AR0234_REG_ORIENTATION_VFLIP;

		ret = ar0234_write_reg(ar0234, AR0234_REG_ORIENTATION,
					AR0234_REG_VALUE_16BIT, reg);
		break;
	}
	case V4L2_CID_VBLANK:
		ret = ar0234_write_reg(ar0234, AR0234_REG_VTS,
				       AR0234_REG_VALUE_16BIT,
				       ar0234->mode.format->height + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		ret = 0;//ar0234_write_reg(ar0234, AR0234_REG_TESTP_RED,
			//	       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		ret = 0;//ar0234_write_reg(ar0234, AR0234_REG_TESTP_GREENR,
			//	       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		ret = 0;//ar0234_write_reg(ar0234, AR0234_REG_TESTP_BLUE,
		//		       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		ret = 0;//ar0234_write_reg(ar0234, AR0234_REG_TESTP_GREENB,
			//	       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	default:
		dev_info(&client->dev,
			"ctrl(id:0x%x,val:0x%x) is not handled\n",
			ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ar0234_ctrl_ops = {
	.s_ctrl = ar0234_set_ctrl,
};

static int ar0234_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	const u32 *codes = ar0234_get_codes(ar0234);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (code->index >= NUM_CODES)
			return -EINVAL;

		code->code = codes[code->index];
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int ar0234_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct ar0234 *ar0234 = to_ar0234(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index >= ARRAY_SIZE(ar0234_formats_24_900))
			return -EINVAL;

		if (fse->code != ar0234_get_format_code(ar0234, fse->code))
			return -EINVAL;

		fse->min_width = ar0234_formats_24_900[fse->index].width;
		fse->max_width = fse->min_width;
		fse->min_height = ar0234_formats_24_900[fse->index].height;
		fse->max_height = fse->min_height;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = AR0234_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = AR0234_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void ar0234_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							fmt->colorspace,
							fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void ar0234_update_image_pad_format(struct ar0234 *ar0234,
					   const struct ar0234_format *format,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = format->width;
	fmt->format.height = format->height;
	fmt->format.field = V4L2_FIELD_NONE;
	ar0234_reset_colorspace(&fmt->format);
}

static void ar0234_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = AR0234_EMBEDDED_LINE_WIDTH;
	fmt->format.height = AR0234_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int __ar0234_get_pad_format(struct ar0234 *ar0234,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *fmt)
{
	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state, fmt->pad);
		/* update the code which could change due to vflip or hflip: */
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				ar0234_get_format_code(ar0234, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			ar0234_update_image_pad_format(
				ar0234, ar0234->mode.format, fmt);
			fmt->format.code = ar0234_get_format_code(
				ar0234, ar0234->fmt.code);
		} else {
			ar0234_update_metadata_pad_format(fmt);
		}
	}

	return 0;
}

static int ar0234_get_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *fmt)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	int ret;

	mutex_lock(&ar0234->mutex);
	ret = __ar0234_get_pad_format(ar0234, sd_state, fmt);
	mutex_unlock(&ar0234->mutex);

	return ret;
}

static int ar0234_get_bit_depth_id(struct ar0234 *ar0234, u32 code,
				   enum ar0234_bit_depth_id *bit_depth_id)
{
	enum ar0234_bit_depth_id i;

	if (!bit_depth_id)
		return -EINVAL;

	u32 const *codes = ar0234_get_codes(ar0234);

	for (i = 0; i < AR0234_BIT_DEPTH_ID_AMOUNT; i++)
		if (codes[i] == code)
			break;

	if (i >= AR0234_BIT_DEPTH_ID_AMOUNT)
		return -ENOENT;

	*bit_depth_id = i;

	return 0;
}

static struct ar0234_timing const *ar0234_get_timing(struct ar0234 *ar0234)
{
	return &ar0234->mode.format->timing[ar0234->hw_config.lane_mode]
				    [ar0234->mode.bit_depth];
}

static void ar0234_set_framing_limits(struct ar0234 *ar0234)
{
	int hblank;
	const struct ar0234_format *format = ar0234->mode.format;
	struct ar0234_timing const *timing = ar0234_get_timing(ar0234);

	/* Update limits and set FPS to default */
	__v4l2_ctrl_modify_range(
		ar0234->vblank, timing->frame_length_lines_min - format->height,
		AR0234_VTS_MAX - format->height, ar0234->vblank->step,
		timing->frame_length_lines_min - format->height);

	/* Setting this will adjust the exposure limits as well */
	__v4l2_ctrl_s_ctrl(ar0234->vblank,
			   timing->frame_length_lines_min - format->height);

	hblank = timing->line_length_pck - format->width;
	__v4l2_ctrl_modify_range(ar0234->hblank, hblank, hblank, 1, hblank);
	__v4l2_ctrl_s_ctrl(ar0234->hblank, hblank);
}

static int ar0234_set_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *fmt)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	struct ar0234_format const *format;
	struct v4l2_mbus_framefmt *framefmt;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&ar0234->mutex);

	if (fmt->pad == IMAGE_PAD) {
		fmt->format.code = ar0234_get_format_code(ar0234,
							fmt->format.code);

		format = v4l2_find_nearest_size(
			ar0234_formats_24_900, ARRAY_SIZE(ar0234_formats_24_900), width,
			height, fmt->format.width, fmt->format.height);
		ar0234_update_image_pad_format(ar0234, format, fmt);
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
								fmt->pad);
			*framefmt = fmt->format;
		} else if (ar0234->mode.format != format ||
			   ar0234->fmt.code != fmt->format.code) {
			ar0234->fmt = fmt->format;
			ar0234->mode.format = format;
			ar0234_get_bit_depth_id(ar0234, fmt->format.code,
						&ar0234->mode.bit_depth);
			ar0234_set_framing_limits(ar0234);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
								fmt->pad);
			*framefmt = fmt->format;
		} else {
			/* Only one embedded data mode is supported */
			ar0234_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&ar0234->mutex);

	return 0;
}

static int ar0234_set_framefmt(struct ar0234 *ar0234)
{
	switch (ar0234->fmt.code) {
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_Y10_1X10:
		return ar0234_write_reg(ar0234, 0x31AC, AR0234_REG_VALUE_16BIT,
					0x0A0A);
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_Y8_1X8:
		return ar0234_write_reg(ar0234, 0x31AC, AR0234_REG_VALUE_16BIT,
					0x0808);
	}

	return -EINVAL;
}

static const struct v4l2_rect *
__ar0234_get_pad_crop(struct ar0234 *ar0234, struct v4l2_subdev_state *sd_state,
			unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_state_get_crop(sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ar0234->mode.format->crop;
	}

	return NULL;
}

static int ar0234_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct ar0234 *ar0234 = to_ar0234(sd);

		mutex_lock(&ar0234->mutex);
		sel->r = *__ar0234_get_pad_crop(ar0234, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&ar0234->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = AR0234_NATIVE_WIDTH;
		sel->r.height = AR0234_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = AR0234_PIXEL_ARRAY_TOP;
		sel->r.left = AR0234_PIXEL_ARRAY_LEFT;
		sel->r.width = AR0234_PIXEL_ARRAY_WIDTH;
		sel->r.height = AR0234_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int ar0234_start_streaming(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	struct ar0234_timing const *timing = ar0234_get_timing(ar0234);

	const struct ar0234_reg_sequence *reg_seq;
	int ret;

	ret = ar0234_write_regs(ar0234, common_init, ARRAY_SIZE(common_init));
	if (ret) {
		dev_err(&client->dev, "%s failed to set common settings\n",
			__func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_seq = &ar0234->mode.format->reg_sequence;
	ret = ar0234_write_regs(ar0234, reg_seq->regs, reg_seq->amount);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	ret = ar0234_set_framefmt(ar0234);
	if (ret) {
		dev_err(&client->dev, "%s failed to set frame format: %d\n",
			__func__, ret);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(ar0234->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return ar0234_write_reg(ar0234, AR0234_REG_RESET,
				AR0234_REG_VALUE_16BIT,
				AR0234_REG_RESET_STREAM_ON);
}

static void ar0234_stop_streaming(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	int ret;

	/* set stream off register */
	ret = ar0234_write_reg(ar0234, AR0234_REG_RESET, AR0234_REG_VALUE_16BIT,
				AR0234_REG_RESET_STREAM_OFF);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int ar0234_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ar0234->mutex);
	if (ar0234->streaming == enable) {
		mutex_unlock(&ar0234->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		/*
		* Apply default & customized values
		* and then start streaming.
		*/
		ret = ar0234_start_streaming(ar0234);
		if (ret)
			goto err_rpm_put;
	} else {
		ar0234_stop_streaming(ar0234);
		pm_runtime_put(&client->dev);
	}

	ar0234->streaming = enable;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(ar0234->vflip, enable);
	__v4l2_ctrl_grab(ar0234->hflip, enable);

	mutex_unlock(&ar0234->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&ar0234->mutex);

	return ret;
}

/* Power/clock management functions */
static int ar0234_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234 *ar0234 = to_ar0234(sd);
	int ret;

	ret = regulator_bulk_enable(AR0234_NUM_SUPPLIES,
				    ar0234->hw_config.supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(ar0234->hw_config.extclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(ar0234->hw_config.gpio_reset, 1);
	usleep_range(AR0234_XCLR_MIN_DELAY_US,
			AR0234_XCLR_MIN_DELAY_US + AR0234_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(AR0234_NUM_SUPPLIES, ar0234->hw_config.supplies);

	return ret;
}

static int ar0234_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234 *ar0234 = to_ar0234(sd);

	gpiod_set_value_cansleep(ar0234->hw_config.gpio_reset, 0);
	regulator_bulk_disable(AR0234_NUM_SUPPLIES, ar0234->hw_config.supplies);
	clk_disable_unprepare(ar0234->hw_config.extclk);

	return 0;
}

/* Verify chip ID */
static int ar0234_identify_module(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	int ret;
	u32 val;

	ret = ar0234_read_reg(ar0234, AR0234_REG_CHIP_ID,
				AR0234_REG_VALUE_16BIT, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %d\n",
			ret);
		return ret;
	}

	if (val != AR0234_CHIP_ID && val != AR0234_CHIP_ID_MONO) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			AR0234_CHIP_ID, val);
		return -EIO;
	}

	dev_info(&client->dev, "Success reading chip id: %x\n", val);

	if (val == AR0234_CHIP_ID_MONO)
		ar0234->monochrome = true;

	return 0;
}

static const struct v4l2_subdev_core_ops ar0234_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ar0234_video_ops = {
	.s_stream = ar0234_set_stream,
};

static const struct v4l2_subdev_pad_ops ar0234_pad_ops = {
	.enum_mbus_code = ar0234_enum_mbus_code,
	.get_fmt = ar0234_get_pad_format,
	.set_fmt = ar0234_set_pad_format,
	.get_selection = ar0234_get_selection,
	.enum_frame_size = ar0234_enum_frame_size,
};

static const struct v4l2_subdev_ops ar0234_subdev_ops = {
	.core = &ar0234_core_ops,
	.video = &ar0234_video_ops,
	.pad = &ar0234_pad_ops,
};

static const struct v4l2_subdev_internal_ops ar0234_internal_ops = {
	.open = ar0234_open,
};

/* Initialize control handlers */
static int ar0234_init_controls(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	int exposure_max, exposure_def;
	struct v4l2_ctrl *ctrl;
	struct ar0234_timing const *timing = ar0234_get_timing(ar0234);
	int i, ret;

	ctrl_hdlr = &ar0234->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
	if (ret)
		return ret;

	mutex_init(&ar0234->mutex);
	ctrl_hdlr->lock = &ar0234->mutex;

	/* By default, PIXEL_RATE is read only */
	ar0234->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
						V4L2_CID_PIXEL_RATE,
						AR0234_PIXEL_RATE,
						AR0234_PIXEL_RATE, 1,
						AR0234_PIXEL_RATE);
	if (ar0234->pixel_rate)
		ar0234->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/*
	 * Create the controls here, but mode specific limits are setup
	 * in the ar0234_set_framing_limits() call below.
	 */
	ar0234->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					   V4L2_CID_VBLANK, 0,
					   0xFFFF, 1, 0);

	ar0234->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xFFFF, 1,
					   0);
	if (ar0234->hblank)
		ar0234->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = timing->frame_length_lines_min - AR0234_EXPOSURE_MIN;
	exposure_def = (exposure_max < AR0234_EXPOSURE_DEFAULT) ?
		exposure_max : AR0234_EXPOSURE_DEFAULT;
	ar0234->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
						V4L2_CID_EXPOSURE,
						AR0234_EXPOSURE_MIN, exposure_max,
						AR0234_EXPOSURE_STEP,
						exposure_def);

	v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			AR0234_ANA_GAIN_MIN, AR0234_ANA_GAIN_MAX,
			AR0234_ANA_GAIN_STEP, AR0234_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			AR0234_DGTL_GAIN_MIN, AR0234_DGTL_GAIN_MAX,
			AR0234_DGTL_GAIN_STEP, AR0234_DGTL_GAIN_DEFAULT);

	ar0234->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					V4L2_CID_HFLIP, 0, 1, 1, 0);

	ar0234->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ar0234_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(ar0234_test_pattern_menu) - 1,
					0, 0, ar0234_test_pattern_menu);
	for (i = 0; i < 4; i++) {
		/*
		* The assumption is that
		* V4L2_CID_TEST_PATTERN_GREENR == V4L2_CID_TEST_PATTERN_RED + 1
		* V4L2_CID_TEST_PATTERN_BLUE   == V4L2_CID_TEST_PATTERN_RED + 2
		* V4L2_CID_TEST_PATTERN_GREENB == V4L2_CID_TEST_PATTERN_RED + 3
		*/
		v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
				V4L2_CID_TEST_PATTERN_RED + i,
				AR0234_TESTP_COLOUR_MIN,
				AR0234_TESTP_COLOUR_MAX,
				AR0234_TESTP_COLOUR_STEP,
				AR0234_TESTP_COLOUR_MAX);
		/* The "Solid color" pattern is white by default */
	}

	ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ar0234_ctrl_ops,
					V4L2_CID_LINK_FREQ, 0, 0,
					link_freq);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (!ret)
		v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ar0234_ctrl_ops,
						&props);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ar0234->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&ar0234->mutex);

	ar0234_set_framing_limits(ar0234);

	mutex_unlock(&ar0234->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&ar0234->mutex);

	return ret;
}

static void ar0234_free_controls(struct ar0234 *ar0234)
{
	v4l2_ctrl_handler_free(ar0234->sd.ctrl_handler);
	mutex_destroy(&ar0234->mutex);
}

static int ar0234_parse_hw_config(struct ar0234 *ar0234)
{
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *endpoint;
	struct ar0234_hw_config *hw_config = &ar0234->hw_config;
	unsigned long extclk_frequency;
	int ret = -EINVAL;
	unsigned int i;

	for (i = 0; i < AR0234_NUM_SUPPLIES; i++)
		hw_config->supplies[i].supply = ar0234_supply_name[i];

	ret = devm_regulator_bulk_get(ar0234->dev, AR0234_NUM_SUPPLIES,
				       hw_config->supplies);
	if (ret) {
		dev_err(ar0234->dev, "failed to get regulators\n");
		return ret;
	}

	/* Get optional reset pin */
	hw_config->gpio_reset =
		devm_gpiod_get_optional(ar0234->dev, "reset", GPIOD_OUT_HIGH);

	/* Get input clock (extclk) */
	hw_config->extclk = devm_clk_get(ar0234->dev, "extclk");
	if (IS_ERR(hw_config->extclk)) {
		if (PTR_ERR(hw_config->extclk) != -EPROBE_DEFER)
			dev_err(ar0234->dev, "failed to get extclk %ld\n",
				PTR_ERR(hw_config->extclk));
		return PTR_ERR(hw_config->extclk);
	}

	endpoint =
		fwnode_graph_get_next_endpoint(dev_fwnode(ar0234->dev), NULL);
	if (!endpoint) {
		dev_err(ar0234->dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(ar0234->dev, "could not parse endpoint\n");
		goto error_out;
	}

	/* Check the number of MIPI CSI2 data lanes */
	switch (ep_cfg.bus.mipi_csi2.num_data_lanes) {
	case 2:
		hw_config->lane_mode = AR0234_LANE_MODE_ID_2;
		break;
	case 4:
		hw_config->lane_mode = AR0234_LANE_MODE_ID_4;
		break;
	default:
		ret = dev_err_probe(ar0234->dev, -EINVAL,
				    "invalid number of CSI2 data lanes %d\n",
				    ep_cfg.bus.mipi_csi2.num_data_lanes);
		goto error_out;
	}

	hw_config->num_data_lanes = ep_cfg.bus.mipi_csi2.num_data_lanes;

	/* Check the link frequency set in device tree */
	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(ar0234->dev,
			"link-frequency property not found in DT\n");
		ret = -EINVAL;
		goto error_out;
	}

	if (ep_cfg.nr_of_link_frequencies != 1 ||
	    ep_cfg.link_frequencies[0] != AR0234_FREQ_LINK_10BIT) {
		dev_err(ar0234->dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
		ret = -EINVAL;
		goto error_out;
	}

	extclk_frequency = clk_get_rate(hw_config->extclk);

	if (extclk_frequency != AR0234_FREQ_EXTCLK) {
		dev_err(ar0234->dev, "extclk frequency not supported: %lu Hz\n",
			extclk_frequency);
		ret = -EINVAL;
		goto error_out;
	}

	dev_info(ar0234->dev,
		"extclk: %luHz, link_frequency: %lluHz, lanes: %d\n",
		extclk_frequency, ep_cfg.link_frequencies[0],
		hw_config->num_data_lanes);

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int ar0234_probe(struct i2c_client *client)
{
	struct ar0234 *ar0234;
	int ret;

	ar0234 = devm_kzalloc(&client->dev, sizeof(*ar0234), GFP_KERNEL);
	if (!ar0234)
		return -ENOMEM;

	ar0234->dev = &client->dev;

	v4l2_i2c_subdev_init(&ar0234->sd, client, &ar0234_subdev_ops);

	/* Check the hardware configuration in device tree */
	if (ar0234_parse_hw_config(ar0234))
		return -EINVAL;

	/*
	* The sensor must be powered for ar0234_identify_module()
	* to be able to read the CHIP_ID register
	*/
	ret = ar0234_power_on(ar0234->dev);
	if (ret)
		return ret;

	ret = ar0234_identify_module(ar0234);
	if (ret)
		goto error_power_off;

	/* Set default mode to max resolution */
	ar0234->mode.format = &ar0234_formats_24_900[0];

	/* sensor doesn't enter LP-11 state upon power up until and unless
	* streaming is started, so upon power up switch the modes to:
	* streaming -> standby
	*/
	ret = ar0234_write_reg(ar0234, AR0234_REG_RESET, AR0234_REG_VALUE_16BIT,
				AR0234_REG_RESET_STREAM_ON);
	if (ret < 0)
		goto error_power_off;
	usleep_range(100, 110);

	/* put sensor back to standby mode */
	ret = ar0234_write_reg(ar0234, AR0234_REG_RESET, AR0234_REG_VALUE_16BIT,
				AR0234_REG_RESET_STREAM_OFF);
	if (ret < 0)
		goto error_power_off;
	usleep_range(100, 110);

	ret = ar0234_init_controls(ar0234);
	if (ret)
		goto error_power_off;

	/* Initialize subdev */
	ar0234->sd.internal_ops = &ar0234_internal_ops;
	ar0234->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;
	ar0234->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pads */
	ar0234->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	ar0234->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize default format */
	ar0234_set_default_format(ar0234);

	ret = media_entity_pads_init(&ar0234->sd.entity, NUM_PADS, ar0234->pad);
	if (ret) {
		dev_err(ar0234->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ar0234->sd);
	if (ret < 0) {
		dev_err(ar0234->dev,
			"failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(ar0234->dev);
	pm_runtime_enable(ar0234->dev);
	pm_runtime_idle(ar0234->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ar0234->sd.entity);

error_handler_free:
	ar0234_free_controls(ar0234);

error_power_off:
	ar0234_power_off(ar0234->dev);

	return ret;
}

static void ar0234_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234 *ar0234 = to_ar0234(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	ar0234_free_controls(ar0234);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ar0234_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id ar0234_dt_ids[] = {
	{ .compatible = "onnn,ar0234cs" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ar0234_dt_ids);

static const struct dev_pm_ops ar0234_pm_ops = {
	SET_RUNTIME_PM_OPS(ar0234_power_off, ar0234_power_on, NULL)
};

static struct i2c_driver ar0234_i2c_driver = {
	.driver = {
		.name = "ar0234",
		.of_match_table	= ar0234_dt_ids,
		.pm = &ar0234_pm_ops,
	},
	.probe = ar0234_probe,
	.remove = ar0234_remove,
};

module_i2c_driver(ar0234_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("OnSemi AR0234 sensor driver");
MODULE_LICENSE("GPL");
