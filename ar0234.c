// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for OnSemi AR0234 cameras.
 *
 * Copyright (C) 2021, Raspberry Pi (Trading) Ltd
 * Copyright (C) 2025, UAB Kurokesu
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
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
/* Registers */
#define AR0234_REG_CHIP_ID CCI_REG16(0x3000)
#define AR0234_REG_Y_ADDR_START CCI_REG16(0x3002)
#define AR0234_REG_X_ADDR_START CCI_REG16(0x3004)
#define AR0234_REG_Y_ADDR_END CCI_REG16(0x3006)
#define AR0234_REG_X_ADDR_END CCI_REG16(0x3008)
#define AR0234_REG_FRAME_LENGTH_LINES CCI_REG16(0x300A)
#define AR0234_REG_EXPOSURE_COARSE CCI_REG16(0x3012)
#define AR0234_REG_RESET CCI_REG16(0x301A)
#define AR0234_REG_MODE_SELECT CCI_REG8(0x301C)
#define AR0234_REG_IMAGE_ORIENTATION CCI_REG8(0x301D)
#define AR0234_REG_VT_PIX_CLK_DIV CCI_REG16(0x302A)
#define AR0234_REG_VT_SYS_CLK_DIV CCI_REG16(0x302C)
#define AR0234_REG_PRE_PLL_CLK_DIV CCI_REG16(0x302E)
#define AR0234_REG_PLL_MULTIPLIER CCI_REG16(0x3030)
#define AR0234_REG_OP_PIX_CLK_DIV CCI_REG16(0x3036)
#define AR0234_REG_OP_SYS_CLK_DIV CCI_REG16(0x3038)
#define AR0234_REG_READ_MODE CCI_REG16(0x3040)
#define AR0234_REG_DIGITAL_GAIN CCI_REG16(0x305E)
#define AR0234_REG_ANALOG_GAIN CCI_REG16(0x3060)
#define AR0234_REG_SMIA_TEST CCI_REG16(0x3064)
#define AR0234_REG_DATAPATH_SELECT CCI_REG16(0x306E)
#define AR0234_REG_TEST_PATTERN_MODE CCI_REG16(0x3070)
#define AR0234_REG_TEST_DATA_RED CCI_REG16(0x3072)
#define AR0234_REG_TEST_DATA_GREENR CCI_REG16(0x3074)
#define AR0234_REG_TEST_DATA_BLUE CCI_REG16(0x3076)
#define AR0234_REG_TEST_DATA_GREENB CCI_REG16(0x3078)
#define AR0234_REG_OPERATION_MODE_CTRL CCI_REG16(0x3082)
#define AR0234_REG_SEQ_DATA_PORT CCI_REG16(0x3086)
#define AR0234_REG_SEQ_CTRL_PORT CCI_REG16(0x3088)
#define AR0234_REG_X_ODD_INC CCI_REG16(0x30A2)
#define AR0234_REG_Y_ODD_INC CCI_REG16(0x30A6)
#define AR0234_REG_DIGITAL_TEST CCI_REG16(0x30B0)
#define AR0234_REG_TEMPSENS_CTRL CCI_REG16(0x30B4)
#define AR0234_REG_AE_LUMA_TARGET CCI_REG16(0x3102)
#define AR0234_REG_DELTA_DK_CONTROL CCI_REG16(0x3180)
#define AR0234_REG_DATA_FORMAT_BITS CCI_REG16(0x31AC)
#define AR0234_REG_SERIAL_FORMAT CCI_REG16(0x31AE)
#define AR0234_REG_FRAME_PREAMBLE CCI_REG16(0x31B0)
#define AR0234_REG_LINE_PREAMBLE CCI_REG16(0x31B2)
#define AR0234_REG_MIPI_TIMING_0 CCI_REG16(0x31B4)
#define AR0234_REG_MIPI_TIMING_1 CCI_REG16(0x31B6)
#define AR0234_REG_MIPI_TIMING_2 CCI_REG16(0x31B8)
#define AR0234_REG_MIPI_TIMING_3 CCI_REG16(0x31BA)
#define AR0234_REG_MIPI_TIMING_4 CCI_REG16(0x31BC)
#define AR0234_REG_COMPANDING CCI_REG16(0x31D0)
#define AR0234_REG_PIX_DEF_ID CCI_REG16(0x31E0)
#define AR0234_REG_MIPI_CNTRL CCI_REG16(0x3354)

/* Chip ID */
#define AR0234_CHIP_ID 0x0A56
#define AR0234_CHIP_ID_MONO 0x1A56

/* Sensor frequencies */
#define AR0234_FREQ_EXTCLK 24000000
#define AR0234_FREQ_PIXCLK_2LANE 45000000
#define AR0234_FREQ_PIXCLK_4LANE 90000000
#define AR0234_FREQ_LINK_8BIT 360000000
#define AR0234_FREQ_LINK_10BIT 450000000

/* Frame timing */
#define AR0234_FLL_MAX 0xFFFF
#define AR0234_VBLANK_MIN 16
#define AR0234_LINE_LENGTH_PCK_DEF 612

/* Exposure control */
#define AR0234_EXPOSURE_MIN 2
#define AR0234_EXPOSURE_STEP 1

/* Analog gain control */
#define AR0234_ANA_GAIN_MIN 0
#define AR0234_ANA_GAIN_MAX 232
#define AR0234_ANA_GAIN_STEP 1
#define AR0234_ANA_GAIN_DEFAULT 0x0

/* Digital gain control */
#define AR0234_DGTL_GAIN_MIN 0x0100
#define AR0234_DGTL_GAIN_MAX 0x0FFF
#define AR0234_DGTL_GAIN_DEFAULT 0x0100
#define AR0234_DGTL_GAIN_STEP 1

/* Test Patterns */
#define AR0234_TESTP_COLOUR_MIN 0
#define AR0234_TESTP_COLOUR_MAX 0x03FF
#define AR0234_TESTP_COLOUR_STEP 1
#define AR0234_TESTP_RED_DEFAULT AR0234_TESTP_COLOUR_MAX
#define AR0234_TESTP_GREENR_DEFAULT 0
#define AR0234_TESTP_BLUE_DEFAULT 0
#define AR0234_TESTP_GREENB_DEFAULT 0

#define AR0234_TEST_PATTERN_DISABLED 0
#define AR0234_TEST_PATTERN_SOLID_COLOR 1
#define AR0234_TEST_PATTERN_VERTICAL_COLOR_BARS 2
#define AR0234_TEST_PATTERN_FADE_TO_GREY 3
#define AR0234_TEST_PATTERN_PN9 4
#define AR0234_TEST_PATTERN_WALKING_1S 256

/* Native and active pixel array sizes */
#define AR0234_NATIVE_WIDTH 1484U
#define AR0234_NATIVE_HEIGHT 856U
#define AR0234_PIXEL_ARRAY_LEFT 6U
#define AR0234_PIXEL_ARRAY_TOP 10U
#define AR0234_PIXEL_ARRAY_WIDTH 1920U
#define AR0234_PIXEL_ARRAY_HEIGHT 1200U

/* Embedded metadata stream buffer size (padding every 4 bytes) */
#define AR0234_MD_PADDING_BYTES (AR0234_PIXEL_ARRAY_WIDTH / 4)
#define AR0234_EMBEDDED_LINE_WIDTH \
	(AR0234_PIXEL_ARRAY_WIDTH + AR0234_MD_PADDING_BYTES)
#define AR0234_NUM_EMBEDDED_LINES 2

/* RESET GPIO */
#define AR0234_RESET_DELAY_MIN_US 6200
#define AR0234_RESET_DELAY_RANGE_US 1000

/* Register address size in bits */
#define AR0234_REG_ADDRESS_BITS 16

/* 1 format code for selected link frequency */
#define AR0234_FMT_CODE_AMOUNT 1

/* Helper macro for declaring ar0234 reg sequence */
#define AR0234_REG_SEQ(_reg_array)                                      \
	{                                                               \
		.regs = (_reg_array), .amount = ARRAY_SIZE(_reg_array), \
	}

#define AR0234_SUPPLY_AMOUNT ARRAY_SIZE(ar0234_supply_names)

enum pad_types {
	IMAGE_PAD,
	METADATA_PAD,
	NUM_PADS,
};

struct ar0234_reg_sequence {
	unsigned int amount;
	const struct cci_reg_sequence *regs;
};

enum ar0234_lane_mode_id {
	AR0234_LANE_MODE_ID_2LANE = 0,
	AR0234_LANE_MODE_ID_4LANE,
	AR0234_LANE_MODE_ID_AMOUNT,
};

/* Mode : resolution and related config&values */
struct ar0234_format {
	/* Frame width */
	unsigned int width;
	/* Frame height */
	unsigned int height;
	/* Analog crop rectangle. */
	struct v4l2_rect crop;

	struct ar0234_reg_sequence reg_sequence;
};

struct ar0234_mode {
	struct ar0234_format const *format;
};

/*
 * PLL config for:
 * External clock - 24MHz
 * Link frequency - 360MHz
 * Bit depth      - 8bit
 */
static const struct cci_reg_sequence ar0234_pll_config_24_360_8bit[] = {
	{ AR0234_REG_VT_PIX_CLK_DIV, 0x0008 },
	{ AR0234_REG_VT_SYS_CLK_DIV, 0x0001 },
	{ AR0234_REG_PRE_PLL_CLK_DIV, 0x0001 },
	{ AR0234_REG_PLL_MULTIPLIER, 0x001E },
	{ AR0234_REG_OP_PIX_CLK_DIV, 0x0008 },
	{ AR0234_REG_OP_SYS_CLK_DIV, 0x0002 },
	{ AR0234_REG_FRAME_PREAMBLE, 0x0080 },
	{ AR0234_REG_LINE_PREAMBLE, 0x005C },
	{ AR0234_REG_MIPI_TIMING_0, 0x5248 },
	{ AR0234_REG_MIPI_TIMING_1, 0x4258 },
	{ AR0234_REG_MIPI_TIMING_2, 0x904C },
	{ AR0234_REG_MIPI_TIMING_3, 0x028B },
	{ AR0234_REG_MIPI_TIMING_4, 0x0D89 },
	{ AR0234_REG_MIPI_CNTRL, 0x002A },
	{ AR0234_REG_DATA_FORMAT_BITS, 0x0808 }, // 8bit in/out
};

/*
 * PLL config for:
 * External clock - 24MHz
 * Link frequency - 450MHz
 * Bit depth      - 10bit
 */
static const struct cci_reg_sequence ar0234_pll_config_24_450_10bit[] = {
	{ AR0234_REG_VT_PIX_CLK_DIV, 0x0005 },
	{ AR0234_REG_VT_SYS_CLK_DIV, 0x0001 },
	{ AR0234_REG_PRE_PLL_CLK_DIV, 0x0008 },
	{ AR0234_REG_PLL_MULTIPLIER, 0x0096 },
	{ AR0234_REG_OP_PIX_CLK_DIV, 0x000A },
	{ AR0234_REG_OP_SYS_CLK_DIV, 0x0001 },
	{ AR0234_REG_FRAME_PREAMBLE, 0X0082 },
	{ AR0234_REG_LINE_PREAMBLE, 0X005C },
	{ AR0234_REG_MIPI_TIMING_0, 0X4248 },
	{ AR0234_REG_MIPI_TIMING_1, 0X4258 },
	{ AR0234_REG_MIPI_TIMING_2, 0X904B },
	{ AR0234_REG_MIPI_TIMING_3, 0X030B },
	{ AR0234_REG_MIPI_TIMING_4, 0X0D89 },
	{ AR0234_REG_MIPI_CNTRL, 0x002B },
	{ AR0234_REG_DATA_FORMAT_BITS, 0x0A0A }, // 10bit in/out
};

static const struct cci_reg_sequence common_init[] = {
	{ AR0234_REG_DIGITAL_TEST, 0x0028 },
	{ AR0234_REG_DATAPATH_SELECT, 0x9010 },
	{ AR0234_REG_OPERATION_MODE_CTRL, 0x0003 },
	{ AR0234_REG_READ_MODE, 0x0000 },
	{ AR0234_REG_COMPANDING, 0x0000 },
	{ AR0234_REG_SEQ_CTRL_PORT, 0x8050 },
	//	{ AR0234_REG_SEQ_DATA_PORT, 0x9237 },
	{ CCI_REG16(0x3096), 0x0280 },
	{ AR0234_REG_PIX_DEF_ID, 0x0003 },
	{ CCI_REG16(0x3F4C), 0x121F },
	{ CCI_REG16(0x3F4E), 0x121F },
	{ CCI_REG16(0x3F50), 0x0B81 },
	{ AR0234_REG_SEQ_CTRL_PORT, 0x81BA },
	{ AR0234_REG_SEQ_DATA_PORT, 0x3D02 },
	{ CCI_REG16(0x3ED2), 0xFA96 },
	{ AR0234_REG_DELTA_DK_CONTROL, 0x824F },
	{ CCI_REG16(0x3ECC), 0x0D42 },
	{ CCI_REG16(0x3ECC), 0x0D42 },
	{ CCI_REG16(0x30F0), 0x2283 },
	{ AR0234_REG_AE_LUMA_TARGET, 0x5000 },
	{ AR0234_REG_TEMPSENS_CTRL, 0x0011 },
	{ CCI_REG16(0x30BA), 0x7626 },
	{ AR0234_REG_RESET, 0x205C },
	{ AR0234_REG_SMIA_TEST, 0x1982 },
};

static const struct cci_reg_sequence ar0234_1920x1200_config[] = {
	{ AR0234_REG_Y_ADDR_START, 0x0008 },
	{ AR0234_REG_X_ADDR_START, 0x0008 },
	{ AR0234_REG_Y_ADDR_END, 0x04b7 },
	{ AR0234_REG_X_ADDR_END, 0x0787 },
	{ AR0234_REG_X_ODD_INC, 0x0001 },
	{ AR0234_REG_Y_ODD_INC, 0x0001 },
};

static const struct cci_reg_sequence ar0234_1280x800_config[] = {
	{ AR0234_REG_Y_ADDR_START, 0x00d0 },
	{ AR0234_REG_X_ADDR_START, 0x0148 },
	{ AR0234_REG_Y_ADDR_END, 0x03ef },
	{ AR0234_REG_X_ADDR_END, 0x0647 },
	{ AR0234_REG_X_ODD_INC, 0x0001 },
	{ AR0234_REG_Y_ODD_INC, 0x0001 },
};

static const char *const ar0234_test_pattern_menu[] = {
	"Disabled", "Color Bars", "Solid Color", "Grey Color Bars", "PN9",
};

static const int ar0234_test_pattern_val[] = {
	AR0234_TEST_PATTERN_DISABLE,	 AR0234_TEST_PATTERN_COLOR_BARS,
	AR0234_TEST_PATTERN_SOLID_COLOR, AR0234_TEST_PATTERN_GREY_COLOR,
	AR0234_TEST_PATTERN_PN9,
};

/* regulator supplies */
static const char *const ar0234_supply_name[] = {
	/* Supplies can be enabled in any order */
	"vana", /* Analog (2.8V) supply */
	"vdig", /* Digital Core (1.8V) supply */
	"vddl", /* IF (1.2V) supply */
};

/* Format configs */
static const struct ar0234_format ar0234_formats[] = {
	{
		.width = 1920,
		.height = 1200,
		.crop = {
			.left = AR0234_PIXEL_ARRAY_LEFT,
			.top = AR0234_PIXEL_ARRAY_TOP,
			.width = 1920,
			.height = 1200,
		},
		.reg_sequence = AR0234_REG_SEQ(ar0234_1920x1200_config),
	},
	{
		.width = 1280,
		.height = 800,
		.crop = {
			.left = 320,
			.top = 200,
			.width = 1280,
			.height = 800,
		},
		.reg_sequence = AR0234_REG_SEQ(ar0234_1280x800_config),
	},
};

struct ar0234_fmt_codes {
	u32 bayer;
	u32 mono;
};

struct ar0234_pll_config {
	s64 freq_link;
	u32 freq_extclk;
	struct ar0234_reg_sequence regs_pll;
	struct ar0234_fmt_codes fmt_codes;
};

static const struct ar0234_pll_config ar0234_pll_configs[] = {
	{
		.freq_link = AR0234_FREQ_LINK_8BIT,
		.freq_extclk = AR0234_FREQ_EXTCLK,
		.regs_pll = AR0234_REG_SEQ(ar0234_pll_config_24_360_8bit),
		.fmt_codes = {
			.bayer = MEDIA_BUS_FMT_SGRBG8_1X8,
			.mono = MEDIA_BUS_FMT_Y8_1X8,
		},
	},
	{
		.freq_link = AR0234_FREQ_LINK_10BIT,
		.freq_extclk = AR0234_FREQ_EXTCLK,
		.regs_pll = AR0234_REG_SEQ(ar0234_pll_config_24_450_10bit),
		.fmt_codes = {
			.bayer = MEDIA_BUS_FMT_SGRBG10_1X10,
			.mono = MEDIA_BUS_FMT_Y10_1X10,
		},
	},
};

/* Pixel clock frequencies are based on lane amount */
static const u32 ar0234_freq_pixclk[] = {
	[AR0234_LANE_MODE_ID_2LANE] = AR0234_FREQ_PIXCLK_2LANE,
	[AR0234_LANE_MODE_ID_4LANE] = AR0234_FREQ_PIXCLK_4LANE,
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
	struct ar0234_pll_config const *pll_config;

	struct regmap *regmap;

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

static u32 ar0234_get_format_code(struct ar0234 *ar0234)
{
	u32 code;

	if (ar0234->monochrome) {
		code = ar0234->pll_config->fmt_codes.mono;
	} else {
		code = ar0234->pll_config->fmt_codes.bayer;
	}

	return code;
}

static void ar0234_set_default_format(struct ar0234 *ar0234)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = &ar0234->fmt;
	fmt->code = ar0234_get_format_code(ar0234);

	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true, fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = ar0234_formats[0].width;
	fmt->height = ar0234_formats[0].height;
	fmt->field = V4L2_FIELD_NONE;

	ar0234->mode.format = &ar0234_formats[0];
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
	try_fmt_img->width = ar0234_formats[0].width;
	try_fmt_img->height = ar0234_formats[0].height;
	try_fmt_img->code = ar0234_get_format_code(ar0234);
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
	int exposure_max =
		ar0234->mode.format->height + ar0234->vblank->val - 1;

	__v4l2_ctrl_modify_range(ar0234->exposure, ar0234->exposure->minimum,
				 exposure_max, ar0234->exposure->step,
				 exposure_max);
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
		ret = cci_write(ar0234->regmap, AR0234_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(ar0234->regmap, AR0234_REG_EXPOSURE_COARSE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = cci_write(ar0234->regmap, AR0234_REG_DIGITAL_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = 0; //ar0234_write_reg(ar0234, AR0234_REG_TEST_PATTERN,
			//	       AR0234_REG_VALUE_16BIT,
			//	       ar0234_test_pattern_val[ctrl->val]);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = cci_write(ar0234->regmap, AR0234_REG_IMAGE_ORIENTATION,
				(ar0234->vflip->val << 1) | ar0234->hflip->val,
				NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(ar0234->regmap, AR0234_REG_FRAME_LENGTH_LINES,
				ar0234->mode.format->height + ctrl->val, NULL);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		ret = 0; //ar0234_write_reg(ar0234, AR0234_REG_TESTP_RED,
			//	       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		ret = 0; //ar0234_write_reg(ar0234, AR0234_REG_TESTP_GREENR,
			//	       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		ret = 0; //ar0234_write_reg(ar0234, AR0234_REG_TESTP_BLUE,
		//		       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		ret = 0; //ar0234_write_reg(ar0234, AR0234_REG_TESTP_GREENB,
			//	       AR0234_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_HBLANK:
		ret = -EINVAL;
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n", ctrl->id,
			 ctrl->val);
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

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (code->index >= AR0234_FMT_CODE_AMOUNT)
			return -EINVAL;

		code->code = ar0234_get_format_code(ar0234);
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
		if (fse->index >= ARRAY_SIZE(ar0234_formats))
			return -EINVAL;

		if (fse->code != ar0234_get_format_code(ar0234))
			return -EINVAL;

		fse->min_width = ar0234_formats[fse->index].width;
		fse->max_width = fse->min_width;
		fse->min_height = ar0234_formats[fse->index].height;
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
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true, fmt->colorspace,
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
					ar0234_get_format_code(ar0234) :
					MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			ar0234_update_image_pad_format(
				ar0234, ar0234->mode.format, fmt);
			fmt->format.code = ar0234_get_format_code(ar0234);
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

static void ar0234_set_framing_limits(struct ar0234 *ar0234)
{
	int hblank;
	const struct ar0234_format *format = ar0234->mode.format;

	/* Update limits and set FPS to default */
	__v4l2_ctrl_modify_range(ar0234->vblank, AR0234_VBLANK_MIN,
				 AR0234_FLL_MAX - format->height,
				 ar0234->vblank->step, AR0234_VBLANK_MIN);

	/* Setting this will adjust the exposure limits as well */
	__v4l2_ctrl_s_ctrl(ar0234->vblank, AR0234_VBLANK_MIN);

	hblank = AR0234_LINE_LENGTH_PCK_DEF - format->width;
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
		fmt->format.code = ar0234_get_format_code(ar0234);

		format = v4l2_find_nearest_size(
			ar0234_formats, ARRAY_SIZE(ar0234_formats), width,
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

static int ar0234_reset(struct ar0234 *ar0234)
{
	int ret;

	/* 20ms */
	usleep_range(20000, 21000);

	ret = cci_write(ar0234->regmap, AR0234_REG_RESET, 0x00D9, NULL);

	/* 200ms */
	usleep_range(200000, 201000);

	return cci_write(ar0234->regmap, AR0234_REG_RESET, 0x2058, &ret);
}

static inline int
ar0234_reg_seq_write(struct regmap *regmap,
		     struct ar0234_reg_sequence const *reg_sequence)
{
	return cci_multi_reg_write(regmap, reg_sequence->regs,
				   reg_sequence->amount, NULL);
}

static int ar0234_start_streaming(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	const struct ar0234_reg_sequence *reg_seq;
	int ret;

	/* Reset */
	ret = ar0234_reset(ar0234);
	if (ret < 0) {
		dev_err(ar0234->dev, "%s failed to reset\n", __func__);
		return ret;
	}

	/* PLL and MIPI config */
	ret = ar0234_reg_seq_write(ar0234->regmap,
				   &ar0234->pll_config->regs_pll);
	if (ret < 0) {
		dev_err(ar0234->dev,
			"%s failed to configure pll/mipi settings\n", __func__);
		return ret;
	}

	/* Configure lane amount */
	ret = cci_write(ar0234->regmap, AR0234_REG_SERIAL_FORMAT,
			(0x0200 | ar0234->hw_config.num_data_lanes), NULL);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed to configure lane amount\n",
			__func__);
		return ret;
	}

	/* Common */
	ret = cci_multi_reg_write(ar0234->regmap, common_init,
				  ARRAY_SIZE(common_init), NULL);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed to set common settings\n",
			__func__);
		return ret;
	}

	/* Apply default values of current frame format */
	ret = ar0234_reg_seq_write(ar0234->regmap,
				   &ar0234->mode.format->reg_sequence);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed to set frame format\n",
			__func__);
		return ret;
	}

	/* Apply customized values from user */
	ret = __v4l2_ctrl_handler_setup(ar0234->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	ret = ar0234_mode_select(ar0234, true);
	return ret;
}

static void ar0234_stop_streaming(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	int ret;

	/* set stream off */
	ret = ar0234_mode_select(ar0234, false);
	if (ret < 0)
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
		dev_err(&client->dev, "%s: failed to enable clock\n", __func__);
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
	int ret;
	u64 reg_val;

	ret = cci_read(ar0234->regmap, AR0234_REG_CHIP_ID, &reg_val, NULL);
	if (ret < 0)
		return dev_err_probe(ar0234->dev, ret,
				     "failed to read chip id\n");

	if (reg_val == AR0234_CHIP_ID_MONO)
		ar0234->monochrome = true;
	else if (reg_val != AR0234_CHIP_ID)
		return dev_err_probe(ar0234->dev, -EIO,
				     "Invalid chip id: 0x%x\n", (u16)reg_val);

	dev_info(&client->dev, "Success reading chip id: 0x%x\n", (u16)reg_val);

	return ret;
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
	struct v4l2_ctrl *ctrl;
	unsigned int pixel_rate;
	int i, ret;

	ctrl_hdlr = &ar0234->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
	if (ret)
		return ret;

	mutex_init(&ar0234->mutex);
	ctrl_hdlr->lock = &ar0234->mutex;

	/* By default, PIXEL_RATE is read only */
	pixel_rate = ar0234_freq_pixclk[ar0234->hw_config.lane_mode];
	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
				 V4L2_CID_PIXEL_RATE, pixel_rate, pixel_rate, 1,
				 pixel_rate);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/*
	 * Create the controls here, but mode specific limits are setup
	 * in the ar0234_set_framing_limits() call below.
	 */
	ar0234->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					   V4L2_CID_VBLANK, 0, 0xFFFF, 1, 0);

	ar0234->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xFFFF, 1, 0);
	if (ar0234->hblank)
		ar0234->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ar0234->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     AR0234_EXPOSURE_MIN, 0xFFFF,
					     AR0234_EXPOSURE_STEP,
					     AR0234_EXPOSURE_MIN);

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
				      &ar0234->pll_config->freq_link);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (!ret)
		v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ar0234_ctrl_ops,
						&props);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n", __func__,
			ret);
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
		hw_config->lane_mode = AR0234_LANE_MODE_ID_2LANE;
		break;
	case 4:
		hw_config->lane_mode = AR0234_LANE_MODE_ID_4LANE;
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

	extclk_frequency = clk_get_rate(hw_config->extclk);

	/*
	 * Check if there exists a sensor mode defined for current EXTCLK
	 * and given lane rate.
	 */
	for (i = 0; i < ARRAY_SIZE(ar0234_pll_configs); i++) {
		if ((ar0234_pll_configs[i].freq_extclk == extclk_frequency) &&
		    (ar0234_pll_configs[i].freq_link ==
		     ep_cfg.link_frequencies[0]))
			break;
	}

	if (i == ARRAY_SIZE(ar0234_pll_configs)) {
		dev_err(ar0234->dev,
			"no valid sensor mode defined for EXTCLK %luHz\
			 and link frequency %lluHz\n",
			extclk_frequency, ep_cfg.link_frequencies[0]);
		ret = -EINVAL;
		goto error_out;
	}

	ar0234->pll_config = &ar0234_pll_configs[i];

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
	ar0234->regmap =
		devm_cci_regmap_init_i2c(client, AR0234_REG_ADDRESS_BITS);
	if (IS_ERR(ar0234->regmap))
		return PTR_ERR(ar0234->regmap);

	/*
	 * Enable power management. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the sensor on manually here, identify it, and fully initialize it.
	 */
	ret = ar0234_power_on(ar0234->dev);
	if (ret)
		return ret;

	pm_runtime_set_active(ar0234->dev);
	pm_runtime_get_noresume(ar0234->dev);
	pm_runtime_enable(ar0234->dev);
	pm_runtime_set_autosuspend_delay(ar0234->dev, 1000);
	pm_runtime_use_autosuspend(ar0234->dev);

	ret = ar0234_identify_module(ar0234);
	if (ret)
		goto error_power_off;

	/* sensor doesn't enter LP-11 state upon power up until and unless
	* streaming is started, so upon power up switch the modes to:
	* streaming -> standby
	*/
	ret = ar0234_mode_select(ar0234, true);
	if (ret < 0)
		goto error_power_off;
	usleep_range(100, 110);

	/* put sensor back to standby mode */
	ret = ar0234_mode_select(ar0234, false);
	if (ret < 0)
		goto error_power_off;
	usleep_range(100, 110);

	/* Initialize default format */
	ar0234_set_default_format(ar0234);

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

	/*
	 * Finally, enable autosuspend and decrease the usage count. The device
	 * will get suspended after the autosuspend delay, turning the power
	 * off.
	 */
	pm_runtime_mark_last_busy(ar0234->dev);
	pm_runtime_put_autosuspend(ar0234->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ar0234->sd.entity);

error_handler_free:
	ar0234_free_controls(ar0234);

error_power_off:
	pm_runtime_disable(ar0234->dev);
	pm_runtime_put_noidle(ar0234->dev);
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

static const struct dev_pm_ops ar0234_pm_ops = { SET_RUNTIME_PM_OPS(
	ar0234_power_off, ar0234_power_on, NULL) };

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
MODULE_AUTHOR("Danius Kalvaitis <danius@kurokesu.com");
MODULE_DESCRIPTION("OnSemi AR0234 sensor driver");
MODULE_LICENSE("GPL");
