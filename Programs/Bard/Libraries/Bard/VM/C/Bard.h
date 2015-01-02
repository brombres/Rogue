#ifndef BARD_H
#define BARD_H

#ifdef __cplusplus
#  define BARD_BEGIN_HEADER extern "C" {
#  define BARD_END_HEADER   }
#else
#  define BARD_BEGIN_HEADER
#  define BARD_END_HEADER
#endif

BARD_BEGIN_HEADER

#define BARD_DEBUG

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#endif // _CRT_SECURE_NO_WARNINGS
#endif

#include "Crom.h"
#include "BardDefines.h"
#include "BardEvents.h"
#include "BardUtil.h"
#include "BardEventQueue.h"
#include "BardMessageManager.h"
#include "BardObject.h"
#include "BardString.h"
#include "BardType.h"
#include "BardFileReader.h"
#include "BardBCLoader.h"
#include "BardProperty.h"
#include "BardMethod.h"
#include "BardMM.h"
#include "BardVM.h"

#include "BardProcessor.h"

#include "BardStandardLibrary.h"
#include "BardArray.h"
#include "BardList.h"

BARD_END_HEADER

#endif // BARD_H
