#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
extern "C"
{
#include "cmu_lts_model.c"

const cst_lts_model* SpeechGen_GetCmuLtsModel()
{
	return cmu_lts_model;
}
}
THIRD_PARTY_INCLUDES_END
