namespace TowerFall;

// Functions implemented by hand in C++ (port/runtime/native.cpp)
public static class Native
{
	public static extern string GetSavePath();

	public static extern string GetPlatform();

	// Gameplay-layer button bits (XNA Buttons values) for a pad. On Wii U, Nintendo-style pads use a
	// different layout in matches (e.g. B = jump) than in menus (A = confirm, B = back).
	public static extern uint GamePadGameButtons(int index);
}
