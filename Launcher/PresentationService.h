/*
** Echelon launcher presentation handoff.
*/

#pragma once

#include <cstdint>
#include <functional>

struct SDL_Window;

namespace EchelonLauncher
{

// Echelon @refactor Codex 07/09/2026 Keep engine presentation requests behind a renderer-neutral host service.
class PresentationService
{
public:
	using ApplyFunction = std::function<bool(bool windowed, uint32_t renderWidth, uint32_t renderHeight)>;

	PresentationService(SDL_Window *window, ApplyFunction applyFunction);

	bool apply(bool windowed, uint32_t renderWidth, uint32_t renderHeight);

private:
	SDL_Window *m_window = nullptr;
	ApplyFunction m_applyFunction;
};

} // namespace EchelonLauncher
