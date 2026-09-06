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
	// Echelon @feature Codex 07/09/2026 Receive renderer-neutral engine events through the shared presentation bridge.
	void onEngineEvent(uint32_t event, uint64_t frameIndex);
	bool engineActive() const { return m_engineActive; }
	bool engineQuiescent() const { return m_engineQuiescent; }
	uint64_t frameCount() const { return m_frameCount; }

private:
	SDL_Window *m_window = nullptr;
	ApplyFunction m_applyFunction;
	bool m_engineActive = false;
	bool m_engineQuiescent = false;
	uint64_t m_frameCount = 0;
};

} // namespace EchelonLauncher
