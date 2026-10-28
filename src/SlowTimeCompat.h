#pragma once

#include "PCH.h"

namespace SlowTimeCompat
{
	bool HasActiveSlowTimeEffect();
	void InstallHooks();
	void RefreshTrackedForms();
	bool TryDeferWheelerRestore(float a_currentBefore, float a_targetAfter, float a_restoreBaseline, float& a_replacementTarget);
	void MaybeOverrideTrackedFinishRestore(float& a_targetAfter);
}
