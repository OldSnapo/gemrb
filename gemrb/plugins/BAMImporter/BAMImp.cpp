/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Header: /data/gemrb/cvs2svn/gemrb/gemrb/gemrb/plugins/BAMImporter/BAMImp.cpp,v 1.10 2003/12/15 09:38:10 balrog994 Exp $
 *
 */

#include "../../includes/win32def.h"
#include "BAMImp.h"
#include "../Core/Interface.h"
#include "../Core/Compressor.h"
#include "../Core/FileStream.h"

BAMImp::BAMImp(void)
{
	str = NULL;
	autoFree = false;
}

BAMImp::~BAMImp(void)
{
	if(str && autoFree)
		delete(str);
}

bool BAMImp::Open(DataStream * stream, bool autoFree)
{
	if(stream == NULL)
		return false;
	if(str && this->autoFree)
		delete(str);
	str = stream;
	this->autoFree = autoFree;
	char Signature[8];
	str->Read(Signature, 8);
	if(strncmp(Signature, "BAMCV1  ", 8) == 0) {
printf("Compressed file found...\n");
		//Check if Decompressed file has already been Cached
		char cpath[_MAX_PATH];
		strcpy(cpath, core->CachePath);
		strcat(cpath, stream->filename);
		FILE * exist_in_cache = fopen(cpath, "rb");
		if(exist_in_cache) {
			//File was previously cached, using local copy
			if(autoFree)
				delete(str);
			fclose(exist_in_cache);
			FileStream * s = new FileStream();
			s->Open(cpath);
			str = s;
			str->Read(Signature, 8);
		}
		else {
			//No file found in Cache, Decompressing and storing for further use
			unsigned long UnCompLength;
			str->Read(&UnCompLength, 4);
			//TODO: Decompress Bam File
			if(!core->IsAvailable(IE_COMPRESSION_CLASS_ID)) {
				printf("No Compression Manager Available.\nCannot Load Compressed Bam File.\n");
				return false;
			}	
			Compressor * comp = (Compressor*)core->GetInterface(IE_COMPRESSION_CLASS_ID);
			void * inbuf = malloc(str->Size()-12);
			void * outbuf = malloc(UnCompLength);
			str->Read(inbuf, str->Size()-12);
			comp->Decompress(outbuf, &UnCompLength, inbuf, str->Size()-12);
			core->FreeInterface(comp);
			free(inbuf);
			if(autoFree)
				delete(str);
			FILE * newfile = fopen(cpath, "wb");
			fwrite(outbuf, 1, UnCompLength, newfile);
			fclose(newfile);
			FileStream * s = new FileStream();
			s->Open(cpath);
			str = s;
			str->Read(Signature, 8);
		}
	}
	if(strncmp(Signature, "BAM V1  ", 8) != 0) {
		return false;
	}
	frames.clear();
	cycles.clear();
	unsigned short	FramesCount;
	unsigned char	CyclesCount;
	str->Read(&FramesCount, 2);
	str->Read(&CyclesCount, 1);
	str->Read(&CompressedColorIndex, 1);
	str->Read(&FramesOffset, 4);
	str->Read(&PaletteOffset, 4);
	str->Read(&FLTOffset, 4);
	str->Seek(FramesOffset, GEM_STREAM_START);
	for(unsigned int i = 0; i < FramesCount; i++) {
		FrameEntry fe;
		str->Read(&fe.Width, 2);
		str->Read(&fe.Height, 2);
		str->Read(&fe.XPos, 2);
		str->Read(&fe.YPos, 2);
		str->Read(&fe.FrameData, 4);
		frames.push_back(fe);
	}
	for(unsigned int i = 0; i < CyclesCount; i++) {
		CycleEntry ce;
		str->Read(&ce.FramesCount, 2);
		str->Read(&ce.FirstFrame, 2);
		cycles.push_back(ce);
	}
	str->Seek(PaletteOffset, GEM_STREAM_START);
	for(unsigned int i = 0; i < 256; i++) {
		RevColor rc;
		str->Read(&rc, 4);
		Palette[i].r = rc.r;
		Palette[i].g = rc.g;
		Palette[i].b = rc.b;
		Palette[i].a = rc.a;
	}
	return true;
}
	
Sprite2D * BAMImp::GetFrameFromCycle(unsigned char Cycle, unsigned short frame)
{
	str->Seek(FLTOffset + (cycles[Cycle].FirstFrame * 2) + (frame * 2), GEM_STREAM_START);
	unsigned short findex;
	str->Read(&findex, 2);
	return GetFrame(findex);
}

Animation * BAMImp::GetAnimation(unsigned char Cycle, int x, int y, unsigned char mode)
{
	str->Seek(FLTOffset + (cycles[Cycle].FirstFrame * 2), GEM_STREAM_START);
	unsigned short *findex = (unsigned short*)malloc(cycles[Cycle].FramesCount*2);
	for(unsigned int i = 0; i < cycles[Cycle].FramesCount; i++) 
		str->Read(&findex[i], 2);
	Animation * anim = new Animation(findex, cycles[Cycle].FramesCount);
	anim->x = x;
	anim->y = y;
	for(int i = 0; i < cycles[Cycle].FramesCount; i++) {
		anim->AddFrame(GetFrame(findex[i], mode), findex[i]);
	}
	return anim;
}

Sprite2D * BAMImp::GetFrame(unsigned short findex, unsigned char mode)
{
	if(findex >= frames.size())
		findex = 0;
	str->Seek((frames[findex].FrameData & 0x7FFFFFFF), GEM_STREAM_START);
	unsigned long pixelcount = frames[findex].Height * frames[findex].Width;
	void * pixels = malloc(pixelcount);
	bool RLECompressed = ((frames[findex].FrameData & 0x80000000) == 0);
	if(RLECompressed) { //if RLE Compressed
		unsigned long RLESize;
		/*if((findex+1) >= frames.size())
			RLESize = str->Size() - (frames[findex].FrameData & 0x7FFFFFFF);
		else
			RLESize = (frames[findex+1].FrameData & 0x7FFFFFFF) - (frames[findex].FrameData & 0x7FFFFFFF);*/
		RLESize = (unsigned long)ceil(frames[findex].Width * frames[findex].Height * 1.5);
		void * inpix = malloc(RLESize);
		unsigned char * p = (unsigned char*)inpix;
		unsigned char * Buffer = (unsigned char*)pixels;
 		str->Read(inpix, RLESize);
		unsigned int i = 0;
		while(i < pixelcount) {
			if(*p == 0) {
				p++;
				memset(&Buffer[i], 0, (*p)+1);
				i+=*p;
			}
			else
				Buffer[i] = *p;
			p++;
			i++;
		}
		free(inpix);
	}
	else {
		str->Read(pixels, pixelcount);
	}
	Sprite2D * spr = core->GetVideoDriver()->CreateSprite8(frames[findex].Width, frames[findex].Height, 8, pixels, Palette, true, 0);
	spr->XPos = frames[findex].XPos;
	spr->YPos = frames[findex].YPos;
	if(mode == IE_SHADED) {
		core->GetVideoDriver()->ConvertToVideoFormat(spr);
		core->GetVideoDriver()->CalculateAlpha(spr);
	}
	return spr;
}

void * BAMImp::GetFramePixels(unsigned short findex, unsigned char mode)
{
	if(findex >= frames.size())
		findex = cycles[0].FirstFrame;
	str->Seek((frames[findex].FrameData & 0x7FFFFFFF), GEM_STREAM_START);
	unsigned long pixelcount = frames[findex].Height * frames[findex].Width;
	void * pixels = malloc(pixelcount);
	bool RLECompressed = ((frames[findex].FrameData & 0x80000000) == 0);
	if(RLECompressed) { //if RLE Compressed
		unsigned long RLESize;
		/*if((findex+1) >= frames.size())
			RLESize = str->Size() - (frames[findex].FrameData & 0x7FFFFFFF);
		else
			RLESize = (frames[findex+1].FrameData & 0x7FFFFFFF) - (frames[findex].FrameData & 0x7FFFFFFF);*/
		RLESize = (unsigned long)ceil(frames[findex].Width * frames[findex].Height * 1.5);
		void * inpix = malloc(RLESize);
		unsigned char * p = (unsigned char*)inpix;
		unsigned char * Buffer = (unsigned char*)pixels;
 		str->Read(inpix, RLESize);
		unsigned int i = 0;
		while(i < pixelcount) {
			if(*p == 0) {
				p++;
				memset(&Buffer[i], 0, (*p)+1);
				i+=*p;
			}
			else
				Buffer[i] = *p;
			p++;
			i++;
		}
		free(inpix);
	}
	else {
		str->Read(pixels, pixelcount);
	}
	return pixels;
}

AnimationFactory * BAMImp::GetAnimationFactory(const char * ResRef, unsigned char mode)
{
	AnimationFactory * af = new AnimationFactory(ResRef);
	int count = 0;
	for(unsigned int i = 0; i < cycles.size(); i++) {
		if(cycles[i].FirstFrame + cycles[i].FramesCount > count)
			count = cycles[i].FirstFrame + cycles[i].FramesCount;
	}
	unsigned short * FLT = (unsigned short*)malloc(count*2);
	str->Seek(FLTOffset, GEM_STREAM_START);
	str->Read(FLT, count*2);
	std::vector<unsigned short> indices;
	for(unsigned int i = 0; i < cycles.size(); i++) {
		unsigned int ff = cycles[i].FirstFrame, lf = ff + cycles[i].FramesCount;
		for(unsigned int f = ff; f < lf; f++) {
			if(FLT[f] == 0xffff)
				continue;
			bool found = false;
			for(unsigned int k = 0; k < indices.size(); k++) {
				if(indices[k] == FLT[f]) {
					found = true;
					break;
				}
			}
			if(!found) {
				if((frames[FLT[f]].Width != 1) && (frames[FLT[f]].Height != 1))
					af->AddFrame(GetFrame(FLT[f], mode), FLT[f]);
				indices.push_back(FLT[f]);
			}
		}
		af->AddCycle(cycles[i]);
	}
	af->LoadFLT(FLT, count);
	free(FLT);
	return af;
}
/** This function will load the Animation as a Font */
Font * BAMImp::GetFont()
{
	printf("Start Getting Font\n");
	//printf("Calculating Font Buffer Max Size...");
	int w = 0,h = 0;
	for(unsigned int i = 0; i < cycles.size(); i++) {
		unsigned int index = cycles[i].FirstFrame;
		if(index >= frames.size())
			continue;
		//printf("[index = %d, w = %d, h = %d]\n", index, frames[index].Width, frames[index].Height);
		if(frames[index].Width > w)
			w = frames[index].Width;
		if(frames[index].Height > h)
			h = frames[index].Height;
	}
	//printf("[maxW = %d, maxH = %d]\n", w, h);
	Font * fnt = new Font(w*(int)cycles.size(), h, Palette, true, 0);
	for(unsigned int i = 0; i < cycles.size(); i++) {
		if(cycles[i].FirstFrame >= frames.size()) {
			fnt->AddChar(NULL, 0, 0, 0, 0);
			continue;
		}
		void * pixels = GetFramePixels(cycles[i].FirstFrame);
		fnt->AddChar(pixels, frames[cycles[i].FirstFrame].Width, frames[cycles[i].FirstFrame].Height, frames[cycles[i].FirstFrame].XPos, frames[cycles[i].FirstFrame].YPos);//GetFrame(cycles[i].FirstFrame, 0));
		free(pixels);
	}
	printf("Font Created\n");
	return fnt;
}
/** Debug Function: Returns the Global Animation Palette as a Sprite2D Object.
If the Global Animation Palette is NULL, returns NULL. */
Sprite2D * BAMImp::GetPalette(){
  unsigned char * pixels = (unsigned char*)malloc(256);
  unsigned char * p = pixels;
  for(int i = 0; i < 256; i++) {
    *p++ = (unsigned char)i;
  }
  return core->GetVideoDriver()->CreateSprite8(16,16,8,pixels, Palette, false);
}
