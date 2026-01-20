#pragma once
#include "resource.h"

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif

#define TEXTSERVICE_MODEL        L"Apartment"
#define TEXTSERVICE_LANGID       MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)
#define TEXTSERVICE_ICON_INDEX   -IDIS_Jyutping
#define TEXTSERVICE_DIC L"Lexicon.txt"

#define IME_MODE_ON_ICON_INDEX      IDI_IME_MODE_ON
#define IME_MODE_OFF_ICON_INDEX     IDI_IME_MODE_OFF
#define IME_DOUBLE_ON_INDEX         IDI_DOUBLE_SINGLE_BYTE_ON
#define IME_DOUBLE_OFF_INDEX        IDI_DOUBLE_SINGLE_BYTE_OFF
#define IME_PUNCTUATION_ON_INDEX    IDI_PUNCTUATION_ON
#define IME_PUNCTUATION_OFF_INDEX   IDI_PUNCTUATION_OFF

#define CANDIDATE_FONT_NAMES L"Shanggu Sans", L"LXGW XiHei CL", L"Source Han Sans K", L"Noto Sans CJK KR", L"IBM Plex Sans TC", L"Microsoft JhengHei UI"
#define CANDIDATE_FONT_SIZE 16
#define NUMBER_LABEL_FONT_NAME L"Consolas"
#define NUMBER_LABEL_FONT_SIZE 13

//---------------------------------------------------------------------
// defined Candidated Window
//---------------------------------------------------------------------
#define CANDIDATE_ROW_HEIGHT			(30)
#define CANDWND_BORDER_WIDTH			(1)
#define CANDIDATE_COMMENT_SPACING		(12.0f)  // Spacing between candidate text and comment
#define CANDIDATE_TEXT_MARGIN			(4.0f)   // Left/right margin for text
#define CANDIDATE_NUMBER_MARGIN         (8.0f)   // Left margin for number label
#define CANDIDATE_TEXT_LEADING          (12.0f)  // Left margin for candidate text

//---------------------------------------------------------------------
// defined modifier
//---------------------------------------------------------------------
#define _TF_MOD_ON_KEYUP_SHIFT_ONLY    (0x00010000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_CONTROL_ONLY  (0x00020000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_ALT_ONLY      (0x00040000 | TF_MOD_ON_KEYUP)

//---------------------------------------------------------------------
// string length of CLSID
//---------------------------------------------------------------------
#define CLSID_STRLEN    (38)  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")
