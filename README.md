# MKArcadeGPDXTool
Tool that extracts/imports Mario Kart Arcade GP DX model files. Well the importing is currently WIP, but exporting .dae files is here!

I'm antsy waiting for the Switch 2 to be hacked so I can dive right into making my first (or possibly THE first) MK World custom character, but since that isn't here yet I figured I'd get some practice in by looking at one of the few remaining unmodded Mario Karts!

## Credits

- **Thanks to [KillzXGaming](https://github.com/killzxgaming)** for their existing work on Switch Toolbox which was a great help for starting this project with, along with identifying and explaining some pieces of data found in the .bin files

- **Thanks to [Wexos](https://github.com/Wexos)** for creating a handy wiki page of this game's BIN/BIKE archive file

## Usage

***Extracting*** Just drag and drop any Arcade GP DX .bin file onto the tool (in file explorer, not after opening)
- If importing a tool export into Maya, you may find Maya likes to skip importing normals to skinned meshes - use the maya python script created next to the .dae to fix!
- Add the 'm' arg after specifying the .bin file in the cmd line to merge submeshes to a full mesh. Additionally, in Maya you should use Edit Mesh > Merge (threshold 0) to fix dupe vertices. This will get the best export for use in another game or project. By default the tool will keep submeshes separate so that it can be ported back to a game file without issue.

***Importing*** Modify an existing .dae export in Maya or Blender and modify it, export .dae, drag and drop your .dae onto the tool.
- MESHES MUST BE SPLIT TO HAVE MAX 6 BONE INFLUENCES (>6 assigned is fine but having >6 assigned bones with non-zero weights is not allowed) THIS WILL BE AUTOMATED SOON
- MESHES MUST BE SPLIT BY MATERIAL TOO (only 1 mat/tex per submesh)
- ALL OG BONES MUST EXIST but this may change once animations can be replaced
- Meshes that are currently split will become submeshes of their main mesh upon import, see guide below on how to pair them to their main mesh
- Maya imports will turn '.' into 'FBXASC045', don't change this, the tool will still recognise this as a period upon reimport
- Modify the (character)Preset.txt file to add/modify materials and other values not handled by a .dae file

**Submeshes in Maya:** All child meshes of a mesh will be treated as submeshes of that parent mesh - unless the mesh is listed in (character)Preset.txt file.
Child submeshes can be named anything (these names will not be saved in the game file), just ensure all 'main' meshes are listed in your preset txt.

*Blender cannot have meshes be children of bones so it works differently*

**Submeshes in Blender:** All submeshes are named the same as their main mesh + with suffix ".xxx" (a period followed by 3 digits), & are also siblings to their main mesh.
All main meshes must be listed in preset txt so submeshes can be found and treated as such.

*If submesh explanation doesn't make sense, just export some characters and open them in these 3D programs to see how submeshes work*

This is my first time making a tool for an overlooked model format like this and I've definitely learned some things. Sonic custom character on the way!

I've only tested on my own PC/OS and people quite often (rip) have problems with my tools that I don't run into so please contact me on Discord @blurro if you have any issues or suggestions!
