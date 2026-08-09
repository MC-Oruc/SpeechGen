#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
extern "C"
{
#include "cmu_lts_rules.c"

const char* const* SpeechGen_GetCmuLtsPhoneTable()
{
	return cmu_lts_phone_table;
}

const cst_lts_addr* SpeechGen_GetCmuLtsLetterIndex()
{
	return cmu_lts_letter_index;
}
}
THIRD_PARTY_INCLUDES_END
