using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

public class MaterialPreset
{
    public float[] Diffuse = { 0.5f, 0.5f, 0.5f, 1.0f };
    public float[] Specular = { 0.7f, 0.7f, 0.7f, 1.0f };
    public float[] Ambience = { 1.0f, 1.0f, 1.0f, 1.0f };
    public float Shiny = 1.0f;
    public int TexAlbedo = -1;
    public int TexSpecular = -1;
    public int TexReflective = -1;
    public int TexEnvironment = -1;
    public int TexNormal = -1;
    public int UnknownVal = 64;
    public float UnknownVal2 = 50.0f;
}

public class TextureName
{
    public string Name;
    public int NamePointer;
}

public static class MaterialPresetLoader
{
    public static List<MaterialPreset> LoadPreset(string presetPath)
    {
        Console.WriteLine("Using material preset file: " + presetPath);

        if (!File.Exists(presetPath))
        {
            Console.Error.WriteLine("failed to open preset file: " + presetPath);
            return new List<MaterialPreset>();
        }

        var materials = new List<MaterialPreset>();
        var textureNames = new List<TextureName>();
        var meshList = new List<string>();
        var animFloatMap = new Dictionary<string, float[]>();
        var currentMat = new MaterialPreset();
        bool haveMaterial = false;
        bool inTextures = false;
        bool inMeshes = false;

        int lineNumber = 0;

        try
        {
            foreach (var rawLine in File.ReadLines(presetPath))
            {
                lineNumber++;
                var line = rawLine.Trim();
                if (string.IsNullOrEmpty(line) || line.StartsWith("/") || line.StartsWith(" "))
                    continue;

                var parts = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length == 0)
                    continue;

                string tag = parts[0];

                if (tag == "#Material")
                {
                    if (haveMaterial)
                        materials.Add(currentMat);

                    currentMat = new MaterialPreset();
                    haveMaterial = true;
                    inTextures = false;
                    inMeshes = false;
                    continue;
                }

                if (tag == "#AnimFloats")
                {
                    if (parts.Length == 8)
                    {
                        var boneName = parts[1];
                        float[] vals = parts.Skip(2).Select(float.Parse).ToArray();
                        animFloatMap[boneName] = vals;
                    }
                    else Console.WriteLine($"Invalid #AnimFloats line on {lineNumber}");
                    continue;
                }

                if (tag == "#Textures")
                {
                    inTextures = true; inMeshes = false; continue;
                }
                if (tag == "#Meshes")
                {
                    inTextures = false; inMeshes = true; continue;
                }

                if (inTextures)
                {
                    if (tag.StartsWith("#")) inTextures = false;
                    else textureNames.Add(new TextureName { Name = line, NamePointer = 0 });
                    continue;
                }

                if (inMeshes)
                {
                    if (tag.StartsWith("#")) inMeshes = false;
                    else meshList.Add(line);
                    continue;
                }

                var s = parts.Skip(1).ToArray();
                try
                {
                    switch (tag)
                    {
                        case "#DIFFUSE": ReadFloats(s, currentMat.Diffuse); break;
                        case "#SPECULAR": ReadFloats(s, currentMat.Specular); break;
                        case "#AMBIENCE": ReadFloats(s, currentMat.Ambience); break;
                        case "#SHINY": currentMat.Shiny = float.Parse(s[0]); break;
                        case "#TEXALBEDO": currentMat.TexAlbedo = int.Parse(s[0]); break;
                        case "#TEXSPECULAR": currentMat.TexSpecular = int.Parse(s[0]); break;
                        case "#TEXREFLECTIVE": currentMat.TexReflective = int.Parse(s[0]); break;
                        case "#TEXENVIRONMENT": currentMat.TexEnvironment = int.Parse(s[0]); break;
                        case "#TEXNORMAL": currentMat.TexNormal = int.Parse(s[0]); break;
                        case "#UNKNOWN": currentMat.UnknownVal = int.Parse(s[0]); break;
                        case "#UNKNOWN2": currentMat.UnknownVal2 = float.Parse(s[0]); break;
                        default:
                            Console.Error.WriteLine($"line {lineNumber}: unknown tag: {tag}");
                            break;
                    }
                }
                catch
                {
                    Console.Error.WriteLine($"line {lineNumber}: invalid data for tag {tag}");
                }
            }

            if (haveMaterial) materials.Add(currentMat);
        }
        catch (Exception e)
        {
            string errMsg = $"Exception while reading preset file at line {lineNumber}: {e.Message}\n";
            Console.Error.WriteLine(errMsg);
            return new List<MaterialPreset>();
        }

        if (textureNames.Count == 0)
            Console.Error.WriteLine("no #Textures block found");
        if (meshList.Count == 0)
            Console.Error.WriteLine("no #Meshes block found");

        Console.WriteLine($"loaded {materials.Count} materials, {textureNames.Count} textures, and {meshList.Count} meshes");
        return materials;
    }

    static void ReadFloats(string[] src, float[] dst)
    {
        for (int i = 0; i < dst.Length && i < src.Length; i++)
            dst[i] = float.Parse(src[i]);
    }
}