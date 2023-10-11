
# Divinity Original Sin 2 16:10 patch for Steam Deck

This patch removes DOS2:DE letterboxing (black bars) on Steam Deck native resolution.
Supported game version: 3.6.117.3735 (latest on Steam as of 11/10/2023), verified on SteamOS 3.4.11.

**Installation:**
- [Download](https://github.com/psykana/dos2deck/releases/latest) latest release
- [Transfer](https://www.youtube.com/watch?v=VfsSCMiZVf4) d3d11.dll to `/home/deck/.local/share/Steam/steamapps/common/Divinity Original Sin 2/DefEd/bin`
- Launch the game, set resolution to 1280x800

**Known issues:**
Short profile name may cause "(Y) Change Profile" text in the main menu to get a little bit off screen.
Since GUI files are the *only* ones that cannot be easily overriden by mods, I decided to leave it be.

Reach out on [Larian Studios discord](https://discord.com/invite/larianstudios) server.

**Credits:**
* [Larian Studios](http://larian.com/), for [Divinity: Original Sin 2](http://store.steampowered.com/app/435150/Divinity_Original_Sin_2/)
* [Valve](https://www.valvesoftware.com/en/) for Steam Deck and Proton
* [Guided Hacking](https://guidedhacking.com) for memory hacking and reverse engineering guides.
* Norbyte, Pip and others at `#dos-modding` for info on game's engine inner works.