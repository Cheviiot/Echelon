/*
** Echelon launcher presentation handoff.
*/

#include "PresentationService.h"

#include "LauncherIntegration/EngineModuleAPI.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace EchelonLauncher
{

PresentationService::PresentationService(SDL_Window *window, ApplyFunction applyFunction)
	: m_window(window), m_applyFunction(std::move(applyFunction))
{
}

bool PresentationService::apply(bool windowed, uint32_t renderWidth, uint32_t renderHeight)
{
	if (!m_window || !m_applyFunction || renderWidth == 0 || renderHeight == 0) return false;
	fprintf(stderr, "[WINDOW-MODE] owner=engine requested=%s render=%ux%u\n",
		windowed ? "windowed" : "fullscreen", renderWidth, renderHeight);
	fflush(stderr);
	return m_applyFunction(windowed, renderWidth, renderHeight);
}

void PresentationService::onEngineEvent(uint32_t event, uint64_t frameIndex)
{
	const char *name = "unknown";
	switch (static_cast<EchelonEnginePresentationEventV1>(event)) {
		case ECHELON_ENGINE_PRESENTATION_STARTED_V1:
			name = "started";
			m_engineActive = true;
			m_engineQuiescent = false;
			m_frameCount = 0;
			break;
		case ECHELON_ENGINE_PRESENTATION_FRAME_V1:
			name = "frame";
			m_engineActive = true;
			m_frameCount = std::max(m_frameCount, frameIndex + 1);
			break;
		case ECHELON_ENGINE_PRESENTATION_STOPPING_V1:
			name = "stopping";
			break;
		case ECHELON_ENGINE_PRESENTATION_QUIESCENT_V1:
			name = "quiescent";
			m_engineActive = false;
			m_engineQuiescent = true;
			break;
		case ECHELON_ENGINE_PRESENTATION_FAILED_V1:
			name = "failed";
			m_engineActive = false;
			m_engineQuiescent = false;
			break;
	}
	fprintf(stderr, "[PRESENTATION-BRIDGE] event=%s frame=%llu\n", name,
		static_cast<unsigned long long>(frameIndex));
	fflush(stderr);
}

} // namespace EchelonLauncher
