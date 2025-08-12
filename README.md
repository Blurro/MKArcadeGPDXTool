# MKArcadeGPDXTool
Tool that extracts/imports Mario Kart Arcade GP DX model files!

I'm antsy waiting for the Switch 2 to be hacked so that I can dive right into making my first (or possibly THE first) MK World custom character, but since that isn't here yet I figured I'd get some practice in by looking at one of the few remaining unmodded Mario Karts!

## Credits

- **Thanks to [KillzXGaming](https://github.com/killzxgaming)** for their existing work on Switch Toolbox which was a great help for starting this project with, along with identifying and explaining some pieces of data found in the .bin files

- **Thanks to [Wexos](https://github.com/Wexos)** for creating a handy wiki page of this game's BIN/BIKE archive file

- **Thanks to [Straky](https://github.com/Str4ky)** who made a very cool GUI to go along with this tool, and set me up with this game and TeknoParrot to begin with

## Usage

***Extracting***
- Hit the Browse button or just drag and drop your .bin file onto the input box!

***Importing***
- Modify an existing .dae export in Maya or Blender and modify it, hit Browse or drag and drop your .dae into the top input box
- (Meshes must be split by material and each have a max of 6 bones) now automatically handled by the tool so don't worry about this
- All OG bones must exist but this may change once animations can be replaced
- Meshes that are currently split will become submeshes of their main mesh upon import, see guide below on how to pair them to their main mesh
- Maya imports will turn '.' into 'FBXASC045', don't change this, the tool will still recognise this as a period upon reimport
- Modify the (character)Preset.txt file to add/modify materials etc, then hit Browse or drag and drop it onto the bottom input box

**Submeshes in Maya:** All child meshes of a mesh will be treated as submeshes of that parent mesh - unless the mesh is listed in (character) Preset.txt file.
Child submeshes can be named anything (these names will not be saved in the game file), just ensure all 'main' meshes are listed in your preset txt.

*Blender cannot have meshes be children of bones so it works differently;*

**Submeshes in Blender:** All submeshes are named the same as their main mesh + with suffix ".xxx" (a period followed by 3 digits), & are also siblings to their main mesh.
All main meshes must be listed in preset txt so submeshes can be found and treated as such.

*If submesh explanation doesn't make sense, just export some characters and open them in these 3D programs to see how submeshes work*

This is my first time making a tool for an overlooked model format like this and I've definitely learned some things. Sonic custom character on the way!

If you have any issues, crashes, suggestions, please contact me on Discord @blurro and I'd be happy to hear from you!
