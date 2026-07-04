#pragma once
#include "resource.h"

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif

#define TEXTSERVICE_MODEL        L"Apartment"
#define TEXTSERVICE_LANGID       MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)
#define TEXTSERVICE_ICON_INDEX   -IDIS_IME
#define TEXTSERVICE_SQLITE_DATA  L"Resources\\ime.sqlite3"

#define INPUT_MODE_CANTONESE_ICON_INDEX  IDI_INPUT_MODE_CANTONESE
#define INPUT_MODE_ABC_ICON_INDEX        IDI_INPUT_MODE_ABC

#define CANDIDATE_FONT_NAMES     L"Shanggu Sans", L"Sarasa Gothic CL", L"IBM Plex Sans TC", L"LXGW XiHei CL", L"Microsoft JhengHei UI", L"MiSans L3"
#define CANDIDATE_FONT_SIZE      16
#define NUMBER_LABEL_FONT_NAME   L"Consolas"
#define NUMBER_LABEL_FONT_SIZE   13

//---------------------------------------------------------------------
// defined Candidated Window
//---------------------------------------------------------------------
#define CANDIDATE_ROW_HEIGHT            (30)
#define CANDWND_BORDER_WIDTH            (1)

// HStack-style row layout: LeftPadding | Number | Spacing | CandidateWord | Spacing | Comment | RightPadding
#define CANDIDATE_ROW_PADDING_LEFT      (10.0f)  // Left padding of the row
#define CANDIDATE_ROW_PADDING_RIGHT     (1.0f)   // Right padding of the row
#define CANDIDATE_NUMBER_SPACING        (12.0f)  // Spacing between number and candidate word
#define CANDIDATE_COMMENT_SPACING       (12.0f)  // Spacing between candidate word and comment

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
