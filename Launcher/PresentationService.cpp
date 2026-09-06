/*
** Echelon launcher presentation handoff.
*/

#include "PresentationService.h"

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

} // namespace EchelonLauncher
