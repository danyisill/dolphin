// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "LightingShaderGen.h"
#include "NativeVertexFormat.h"
#include "XFMemory.h"

#define WRITE p+=sprintf

int GetLightingShaderId(u32* out)
{
	for (u32 i = 0; i < xfregs.numChan.numColorChans; ++i)
	{
		out[i] = xfregs.color[i].enablelighting ?
			(u32)xfregs.color[i].hex :
			(u32)xfregs.color[i].matsource;
		out[i] |= (xfregs.alpha[i].enablelighting ?
			(u32)xfregs.alpha[i].hex :
			(u32)xfregs.alpha[i].matsource) << 15;
	}
	_assert_(xfregs.numChan.numColorChans <= 2);
	return xfregs.numChan.numColorChans;
}

// coloralpha - 1 if color, 2 if alpha
char *GenerateLightShader(char *p, int index, const LitChannel& chan, const char* lightsName, int coloralpha)
{
	const char* swizzle = "xyzw";

	if (coloralpha == 1 )
		swizzle = "xyz";
	else if (coloralpha == 2 )
		swizzle = "w";

	if (!(chan.attnfunc & 1))
	{
		// attenuation disabled
		switch (chan.diffusefunc)
		{
			case LIGHTDIF_NONE:
				WRITE(p, "lacc.%s += %s[%d].%s;\n", swizzle, lightsName, index * 5, swizzle);
				break;
			case LIGHTDIF_SIGN:
			case LIGHTDIF_CLAMP:
				WRITE(p, "ldir = normalize(%s[%d + 3].xyz - pos.xyz);\n", lightsName, index * 5);
				WRITE(p, "lacc.%s += %sdot(ldir, _norm0)) * %s[%d].%s;\n",
					swizzle, chan.diffusefunc != LIGHTDIF_SIGN ? "max(0.0f," :"(", lightsName, index * 5, swizzle);
				break;
			default: _assert_(0);
		}
	}
	else // spec and spot
	{
		if (chan.attnfunc == 3) 
		{ // spot
			WRITE(p, "ldir = %s[%d + 3].xyz - pos.xyz;\n", lightsName, index * 5);
			WRITE(p, "dist2 = dot(ldir, ldir);\n"
				"dist = sqrt(dist2);\n"
				"ldir = ldir / dist;\n"
				"attn = max(0.0f, dot(ldir, %s[%d + 4].xyz));\n", lightsName, index * 5);
			WRITE(p, "attn = max(0.0f, dot(%s[%d + 1].xyz, float3(1.0f, attn, attn*attn))) / dot(%s[%d + 2].xyz, float3(1.0f,dist,dist2));\n", lightsName, index * 5, lightsName, index * 5);
		}
		else if (chan.attnfunc == 1) 
		{ // specular
			WRITE(p, "ldir = normalize(%s[%d + 3].xyz);\n", lightsName, index * 5);
			WRITE(p, "attn = (dot(_norm0,ldir) >= 0.0f) ? max(0.0f, dot(_norm0, %s[%d + 4].xyz)) : 0.0f;\n", lightsName, index * 5);
			WRITE(p, "attn = max(0.0f, dot(%s[%d + 1].xyz, float3(1,attn,attn*attn))) / dot(%s[%d + 2].xyz, float3(1,attn,attn*attn));\n", lightsName, index * 5, lightsName, index * 5);
		}

		switch (chan.diffusefunc)
		{
			case LIGHTDIF_NONE:
				WRITE(p, "lacc.%s += attn * %s[%d].%s;\n", swizzle, lightsName, index * 5, swizzle);
				break;
			case LIGHTDIF_SIGN:
			case LIGHTDIF_CLAMP:
				WRITE(p, "lacc.%s += attn * %sdot(ldir, _norm0)) * %s[%d].%s;\n",
					swizzle, 
					chan.diffusefunc != LIGHTDIF_SIGN ? "max(0.0f," :"(",
					lightsName,
					index * 5,
					swizzle);
				break;
			default: _assert_(0);
		}
	}
	WRITE(p, "\n");
	return p;
}

// vertex shader
// lights/colors
// materials name is I_MATERIALS in vs and I_PMATERIALS in ps
// inColorName is color in vs and colors_ in ps
// dest is o.colors_ in vs and colors_ in ps
char *GenerateLightingShader(char *p, int components, const char* materialsName, const char* lightsName, const char* inColorName, const char* dest)
{
	for (unsigned int j = 0; j < xfregs.numChan.numColorChans; j++)
	{
		const LitChannel& color = xfregs.color[j];
		const LitChannel& alpha = xfregs.alpha[j];

		WRITE(p, "{\n");
		
		if (color.matsource) // from vertex
		{
			if (components & (VB_HAS_COL0 << j))
				WRITE(p, "mat = %s%d;\n", inColorName, j);
			else if (components & VB_HAS_COL0)
				WRITE(p, "mat = %s0;\n", inColorName);
			else
				WRITE(p, "mat = float4(1.0f, 1.0f, 1.0f, 1.0f);\n");
		}
		else // from color
		{
			WRITE(p, "mat = %s[%d];\n", materialsName, j+2);
		}

		if (color.enablelighting)
		{
			if (color.ambsource) // from vertex
			{
				if (components & (VB_HAS_COL0<<j) )
					WRITE(p, "lacc = %s%d;\n", inColorName, j);
				else if (components & VB_HAS_COL0 )
					WRITE(p, "lacc = %s0;\n", inColorName);
				else
					WRITE(p, "lacc = float4(0.0f, 0.0f, 0.0f, 0.0f);\n");
			}
			else // from color
			{
				WRITE(p, "lacc = %s[%d];\n", materialsName, j);
			}
		}
		else
		{
			WRITE(p, "lacc = float4(1.0f, 1.0f, 1.0f, 1.0f);\n");
		}

		// check if alpha is different
		if (alpha.matsource != color.matsource)
		{
			if (alpha.matsource) // from vertex
			{
				if (components & (VB_HAS_COL0<<j))
					WRITE(p, "mat.w = %s%d.w;\n", inColorName, j);
				else if (components & VB_HAS_COL0)
					WRITE(p, "mat.w = %s0.w;\n", inColorName);
				else WRITE(p, "mat.w = 1.0f;\n");
			}
			else // from color
			{
				WRITE(p, "mat.w = %s[%d].w;\n", materialsName, j+2);
			}
		}

		if (alpha.enablelighting)
		{
			if (alpha.ambsource) // from vertex
			{
				if (components & (VB_HAS_COL0<<j) )
					WRITE(p, "lacc.w = %s%d.w;\n", inColorName, j);
				else if (components & VB_HAS_COL0 )
					WRITE(p, "lacc.w = %s0.w;\n", inColorName);
				else
					WRITE(p, "lacc.w = 0.0f;\n");
			}
			else // from color
			{
				WRITE(p, "lacc.w = %s[%d].w;\n", materialsName, j);
			}
		}
		else
		{
			WRITE(p, "lacc.w = 1.0f;\n");
		}
		
		if(color.enablelighting && alpha.enablelighting)
		{
			// both have lighting, test if they use the same lights
			int mask = 0;
			if(color.lightparams == alpha.lightparams)
			{
				mask = color.GetFullLightMask() & alpha.GetFullLightMask();
				if(mask)
				{
					for (int i = 0; i < 8; ++i)
					{
						if (mask & (1<<i))
							p = GenerateLightShader(p, i, color, lightsName, 3);
					}
				}
			}

			// no shared lights
			for (int i = 0; i < 8; ++i)
			{
				if (!(mask&(1<<i)) && (color.GetFullLightMask() & (1<<i)))
					p = GenerateLightShader(p, i, color, lightsName, 1);
				if (!(mask&(1<<i)) && (alpha.GetFullLightMask() & (1<<i)))
					p = GenerateLightShader(p, i, alpha, lightsName, 2);
			}
		}
		else if (color.enablelighting || alpha.enablelighting)
		{
			// lights are disabled on one channel so process only the active ones
			const LitChannel& workingchannel = color.enablelighting ? color : alpha;
			int coloralpha = color.enablelighting ? 1 : 2;
			for (int i = 0; i < 8; ++i)
			{
				if (workingchannel.GetFullLightMask() & (1<<i))
					p = GenerateLightShader(p, i, workingchannel, lightsName, coloralpha);
			}
		}
		WRITE(p, "%s%d = mat * clamp(lacc, 0.0, 1.0);\n", dest, j);
		WRITE(p, "}\n");
	}

	return p;
}
