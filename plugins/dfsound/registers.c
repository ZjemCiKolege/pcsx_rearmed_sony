/***************************************************************************
                         registers.c  -  description
                             -------------------
    begin                : Wed May 15 2002
    copyright            : (C) 2002 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

#include "stdafx.h"

#define _IN_REGISTERS

#include "externals.h"
#include "registers.h"
#include "spu_config.h"
#include "../../libpcsxcore/title.h"

static void SoundOn(int start,int end,unsigned short val);
static void SoundOff(int start,int end,unsigned short val);
static void FModOn(int start,int end,unsigned short val);
static void NoiseOn(int start,int end,unsigned short val);
static void SetVolumeL(unsigned char ch,short vol);
static void SetVolumeR(unsigned char ch,short vol);
static void SetPitch(int ch,unsigned short val);
static void ReverbOn(int start,int end,unsigned short val);
static int  SetDisableVolumeChange(short vol);

////////////////////////////////////////////////////////////////////////
// WRITE REGISTERS: called by main emu
////////////////////////////////////////////////////////////////////////

static const uint32_t ignore_dupe[8] = {
 // ch 0-15  c40         c80         cc0
 0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f,
 // ch 16-24 d40         control     reverb
 0x7f7f7f7f, 0x7f7f7f7f, 0xff05ff0f, 0xffffffff
};

void CALLBACK SPUwriteRegister(unsigned long reg, unsigned short val,
 unsigned int cycles)
{
 int r = reg & 0xfff;
 int rofs = (r - 0xc00) >> 1;
 int changed = spu.regArea[rofs] != val;
 spu.regArea[rofs] = val;

 static int isAlreadyRunBiosLogo = 0;
 static int spuOn2OffCnt = 0;
 unsigned short former_spuCtrl;

 if (!changed && (ignore_dupe[rofs >> 5] & (1 << (rofs & 0x1f))))
  return;
 // zero keyon/keyoff?
 if (val == 0 && (r & 0xff8) == 0xd88)
  return;

 do_samples_if_needed(cycles, 0);

 if(r>=0x0c00 && r<0x0d80)                             // some channel info?
  {
   int ch=(r>>4)-0xc0;                                 // calc channel
   switch(r&0x0f)
    {
     //------------------------------------------------// r volume
     case 0:                                           
       SetVolumeL((unsigned char)ch,val);
       break;
     //------------------------------------------------// l volume
     case 2:                                           
       SetVolumeR((unsigned char)ch,val);
       break;
     //------------------------------------------------// pitch
     case 4:                                           
       SetPitch(ch,val);
       goto upd_irq;
     //------------------------------------------------// start
     case 6:      
       // taken from regArea later
       break;
     //------------------------------------------------// level with pre-calcs
     case 8:
       {
        const unsigned long lval=val;
        //---------------------------------------------//
        spu.s_chan[ch].ADSRX.AttackModeExp=(lval&0x8000)?1:0;
        spu.s_chan[ch].ADSRX.AttackRate=(lval>>8) & 0x007f;
        spu.s_chan[ch].ADSRX.DecayRate=(lval>>4) & 0x000f;
        spu.s_chan[ch].ADSRX.SustainLevel=lval & 0x000f;
        //---------------------------------------------//
       }
      break;
     //------------------------------------------------// adsr times with pre-calcs
     case 10:
      {
       const unsigned long lval=val;

       //----------------------------------------------//
       spu.s_chan[ch].ADSRX.SustainModeExp = (lval&0x8000)?1:0;
       spu.s_chan[ch].ADSRX.SustainIncrease= (lval&0x4000)?0:1;
       spu.s_chan[ch].ADSRX.SustainRate = (lval>>6) & 0x007f;
       spu.s_chan[ch].ADSRX.ReleaseModeExp = (lval&0x0020)?1:0;
       if (isTitleName(PARASITE_EVE_DISC_1_JP) ||
               isTitleName(PARASITE_EVE_DISC_1_US) ||
               isTitleName(PARASITE_EVE_DISC_2_JP) ||
               isTitleName(PARASITE_EVE_DISC_2_US)) {
           spu.s_chan[ch].ADSRX.ReleaseRate = ((lval & 0x001f)*9)/10;
       } else {
           spu.s_chan[ch].ADSRX.ReleaseRate = lval & 0x001f;
       }
       //----------------------------------------------//
      }
     break;
     //------------------------------------------------// adsr volume... mmm have to investigate this
     case 12:
       break;
     //------------------------------------------------//
     case 14:                                          // loop?
       spu.s_chan[ch].pLoop=spu.spuMemC+((val&~1)<<3);
       goto upd_irq;
     //------------------------------------------------//
    }
   return;
  }

 switch(r)
   {
    //-------------------------------------------------//
    case H_SPUaddr:
      spu.spuAddr = (unsigned long) val<<3;
      break;
    //-------------------------------------------------//
    case H_SPUdata:
      *(unsigned short *)(spu.spuMemC + spu.spuAddr) = val;
      spu.spuAddr += 2;
      spu.spuAddr &= 0x7fffe;
      break;
    //-------------------------------------------------//
    case H_SPUctrl:
      if (!(spu.spuCtrl & CTRL_IRQ)) {
        spu.spuStat&=~STAT_IRQ;
        if (val & CTRL_IRQ)
         schedule_next_irq();
      }

      if (isAlreadyRunBiosLogo == 0) {
	former_spuCtrl = spu.spuCtrl;

	if ((!(val & 0x8000)) && (former_spuCtrl & 0x8000)) {
	  // waiting until sound data has been played.
	  while (SOUND_isPlaying()) {};
	  
	  if (++spuOn2OffCnt > 2) {
	    isAlreadyRunBiosLogo = 1;
	  }
	}
      }
      spu.spuCtrl=val;
      break;
    //-------------------------------------------------//
    case H_SPUstat:
      spu.spuStat=val&0xf800;
      break;
    //-------------------------------------------------//
    case H_SPUReverbAddr:
      goto rvbd;
    //-------------------------------------------------//
    case H_SPUirqAddr:
      spu.pSpuIrq=spu.spuMemC+(((unsigned long) val<<3)&~0xf);
      goto upd_irq;
    //-------------------------------------------------//
    case H_SPUrvolL:
      spu.rvb->VolLeft=val;
      break;
    //-------------------------------------------------//
    case H_SPUrvolR:
      spu.rvb->VolRight=val;
      break;
    //-------------------------------------------------//

/*
    case H_ExtLeft:
     //auxprintf("EL %d\n",val);
      break;
    //-------------------------------------------------//
    case H_ExtRight:
     //auxprintf("ER %d\n",val);
      break;
*/
    //-------------------------------------------------//
    case H_SPUmvolL:
      if (isTitleName(ARMORED_CORE_JP)) {
      	double percent = (double)val / ARMORED_CORE_JP_MAX_VOLUME;
      	spu_config.iVolume = 768 * percent;
      } else if (isTitleName(CRASH_BANDICOOT_2_EU) ||
      	isTitleName(CRASH_BANDICOOT_2_JP) ||
      	isTitleName(CRASH_BANDICOOT_2_US) ||
      	isTitleName(JUMPING_FLASH_EU) ||
      	isTitleName(JUMPING_FLASH_JP) ||
      	isTitleName(JUMPING_FLASH_US) ||
      	isTitleName(METAL_GEAR_SOLID_DISC_1_EU) ||
      	isTitleName(METAL_GEAR_SOLID_DISC_2_EU) ||
      	isTitleName(METAL_GEAR_SOLID_DISC_1_JP) ||
      	isTitleName(METAL_GEAR_SOLID_DISC_2_JP) ||
      	isTitleName(METAL_GEAR_SOLID_DISC_1_US) ||
      	isTitleName(METAL_GEAR_SOLID_DISC_2_US) ||
      	isTitleName(SILENT_HILL_EU) ||
      	isTitleName(SILENT_HILL_JP) ||
      	isTitleName(SILENT_HILL_US)) {
      	if (val != 0) {
      		spu.dwChannelOn = 0xffffff;
      		spu_config.iVolume = 768;
      	} else {
      		spu.dwChannelOn = 0;
      		spu_config.iVolume = 0;
      	}
      } else if (isTitleName(TOM_CLANCYS_RAINBOW_SIX_EU)) {
      	double percent = (double)val / TOM_CLANCYS_RAINBOW_SIX_EU_MAX_VOLUME;
      	spu_config.iVolume = 768 * percent;
      }
      break;
    //-------------------------------------------------//
    case H_SPUmvolR:
      if (isTitleName(ARMORED_CORE_JP)) {
      	double percent = (double)val / ARMORED_CORE_JP_MAX_VOLUME;
      	spu_config.iVolume = 768 * percent;
      } else if (isTitleName(CRASH_BANDICOOT_2_EU) ||
        isTitleName(CRASH_BANDICOOT_2_JP) ||
        isTitleName(CRASH_BANDICOOT_2_US) ||
        isTitleName(JUMPING_FLASH_EU) ||
        isTitleName(JUMPING_FLASH_JP) ||
        isTitleName(JUMPING_FLASH_US) ||
        isTitleName(METAL_GEAR_SOLID_DISC_1_EU) ||
        isTitleName(METAL_GEAR_SOLID_DISC_2_EU) ||
        isTitleName(METAL_GEAR_SOLID_DISC_1_JP) ||
        isTitleName(METAL_GEAR_SOLID_DISC_2_JP) ||
        isTitleName(METAL_GEAR_SOLID_DISC_1_US) ||
        isTitleName(METAL_GEAR_SOLID_DISC_2_US) ||
        isTitleName(SILENT_HILL_EU) ||
        isTitleName(SILENT_HILL_JP) ||
        isTitleName(SILENT_HILL_US)) {
      	if (val != 0) {
      		spu.dwChannelOn = 0xffffff;
      		spu_config.iVolume = 768;
      	} else {
      		spu.dwChannelOn = 0;
      		spu_config.iVolume = 0;
      	}
      } else if (isTitleName(TOM_CLANCYS_RAINBOW_SIX_EU)) {
      	double percent = (double)val / TOM_CLANCYS_RAINBOW_SIX_EU_MAX_VOLUME;
      	spu_config.iVolume = 768 * percent;
      }
      break;
    //-------------------------------------------------//

/*
    case H_SPUMute1:
     //auxprintf("M0 %04x\n",val);
      break;
    //-------------------------------------------------//
    case H_SPUMute2:
     //auxprintf("M1 %04x\n",val);
      break;
*/
    //-------------------------------------------------//
    case H_SPUon1:
      SoundOn(0,16,val);
      break;
    //-------------------------------------------------//
     case H_SPUon2:
      SoundOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_SPUoff1:
      SoundOff(0,16,val);
      break;
    //-------------------------------------------------//
    case H_SPUoff2:
      SoundOff(16,24,val);
      break;
    //-------------------------------------------------//
    case H_CDLeft:
      if (isTitleName(TOSHINDEN_EU) ||
        isTitleName(TOSHINDEN_JP) ||
        isTitleName(TOSHINDEN_US)) {
        unsigned short max = isTitleName(TOSHINDEN_JP) ?
          TOSHINDEN_JP_DEFAULT_VOLUME : TOSHINDEN_DEFAULT_VOLUME;
        if (val == max) {
          break;
        }
      }

	if (isTitleName(GRADIUS_GAIDEN_JP)) {
		if (State_CdlPause == 1) {
			xaSave_iLeftVolume = val & 0x7fff;
			break;
		}
	}

      spu.iLeftXAVol=val  & 0x7fff;
      if(spu.cddavCallback) spu.cddavCallback(0,val);
      break;
    case H_CDRight:
	if (isTitleName(GRADIUS_GAIDEN_JP)) {
		if (State_CdlPause == 1) {
			xaSave_iRightVolume = val & 0x7fff;
			break;
		}
	}

      spu.iRightXAVol=val & 0x7fff;
      if(spu.cddavCallback) spu.cddavCallback(1,val);
      break;
    //-------------------------------------------------//
    case H_FMod1:
      FModOn(0,16,val);
      break;
    //-------------------------------------------------//
    case H_FMod2:
      FModOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_Noise1:
      NoiseOn(0,16,val);
      break;
    //-------------------------------------------------//
    case H_Noise2:
      NoiseOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_RVBon1:
      ReverbOn(0,16,val);
      break;
    //-------------------------------------------------//
    case H_RVBon2:
      ReverbOn(16,24,val);
      break;
    //-------------------------------------------------//
    case H_Reverb+0   : goto rvbd;
    case H_Reverb+2   : goto rvbd;
    case H_Reverb+4   : spu.rvb->IIR_ALPHA=(short)val;   break;
    case H_Reverb+6   : spu.rvb->ACC_COEF_A=(short)val;  break;
    case H_Reverb+8   : spu.rvb->ACC_COEF_B=(short)val;  break;
    case H_Reverb+10  : spu.rvb->ACC_COEF_C=(short)val;  break;
    case H_Reverb+12  : spu.rvb->ACC_COEF_D=(short)val;  break;
    case H_Reverb+14  : spu.rvb->IIR_COEF=(short)val;    break;
    case H_Reverb+16  : spu.rvb->FB_ALPHA=(short)val;    break;
    case H_Reverb+18  : spu.rvb->FB_X=(short)val;        break;
    case H_Reverb+20  : goto rvbd;
    case H_Reverb+22  : goto rvbd;
    case H_Reverb+24  : goto rvbd;
    case H_Reverb+26  : goto rvbd;
    case H_Reverb+28  : goto rvbd;
    case H_Reverb+30  : goto rvbd;
    case H_Reverb+32  : goto rvbd;
    case H_Reverb+34  : goto rvbd;
    case H_Reverb+36  : goto rvbd;
    case H_Reverb+38  : goto rvbd;
    case H_Reverb+40  : goto rvbd;
    case H_Reverb+42  : goto rvbd;
    case H_Reverb+44  : goto rvbd;
    case H_Reverb+46  : goto rvbd;
    case H_Reverb+48  : goto rvbd;
    case H_Reverb+50  : goto rvbd;
    case H_Reverb+52  : goto rvbd;
    case H_Reverb+54  : goto rvbd;
    case H_Reverb+56  : goto rvbd;
    case H_Reverb+58  : goto rvbd;
    case H_Reverb+60  : spu.rvb->IN_COEF_L=(short)val;   break;
    case H_Reverb+62  : spu.rvb->IN_COEF_R=(short)val;   break;
   }
 return;

upd_irq:
 if (spu.spuCtrl & CTRL_IRQ)
  schedule_next_irq();
 return;

rvbd:
 spu.rvb->dirty = 1; // recalculate on next update
}

////////////////////////////////////////////////////////////////////////
// READ REGISTER: called by main emu
////////////////////////////////////////////////////////////////////////

unsigned short CALLBACK SPUreadRegister(unsigned long reg)
{
 const unsigned long r=reg&0xfff;
        
 if(r>=0x0c00 && r<0x0d80)
  {
   switch(r&0x0f)
    {
     case 12:                                          // get adsr vol
      {
       const int ch=(r>>4)-0xc0;
       if(spu.dwNewChannel&(1<<ch)) return 1;          // we are started, but not processed? return 1
       if((spu.dwChannelOn&(1<<ch)) &&                 // same here... we haven't decoded one sample yet, so no envelope yet. return 1 as well
          !spu.s_chan[ch].ADSRX.EnvelopeVol)
        return 1;
       return (unsigned short)(spu.s_chan[ch].ADSRX.EnvelopeVol>>16);
      }

     case 14:                                          // get loop address
      {
       const int ch=(r>>4)-0xc0;
       return (unsigned short)((spu.s_chan[ch].pLoop-spu.spuMemC)>>3);
      }
    }
  }

 switch(r)
  {
    case H_SPUctrl:
     return spu.spuCtrl;

    case H_SPUstat:
     return spu.spuStat;
        
    case H_SPUaddr:
     return (unsigned short)(spu.spuAddr>>3);

    case H_SPUdata:
     {
      unsigned short s = *(unsigned short *)(spu.spuMemC + spu.spuAddr);
      spu.spuAddr += 2;
      spu.spuAddr &= 0x7fffe;
      return s;
     }

    //case H_SPUIsOn1:
    // return IsSoundOn(0,16);

    //case H_SPUIsOn2:
    // return IsSoundOn(16,24);
 
  }

 return spu.regArea[(r-0xc00)>>1];
}
 
////////////////////////////////////////////////////////////////////////
// SOUND ON register write
////////////////////////////////////////////////////////////////////////

static void SoundOn(int start,int end,unsigned short val)
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if((val&1) && regAreaGet(ch,6))                     // mmm... start has to be set before key on !?!
    {
     spu.s_chan[ch].pCurr=spu.spuMemC+((regAreaGet(ch,6)&~1)<<3); // must be block aligned
     spu.s_chan[ch].pLoop=spu.spuMemC+((regAreaGet(ch,14)&~1)<<3);
     spu.dwNewChannel|=(1<<ch);
    }
  }
}

////////////////////////////////////////////////////////////////////////
// SOUND OFF register write
////////////////////////////////////////////////////////////////////////

static void SoundOff(int start,int end,unsigned short val)
{
 int ch;
 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if(val&1)
    {
     spu.s_chan[ch].ADSRX.State = ADSR_RELEASE;

     // Jungle Book - Rhythm 'n Groove
     // - turns off buzzing sound (loop hangs)
     spu.dwNewChannel &= ~(1<<ch);
    }                                                  
  }
}

////////////////////////////////////////////////////////////////////////
// FMOD register write
////////////////////////////////////////////////////////////////////////

static void FModOn(int start,int end,unsigned short val)
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   if(val&1)                                           // -> fmod on/off
    {
     if(ch>0) 
      {
       spu.s_chan[ch].bFMod=1;                         // --> sound channel
       spu.s_chan[ch-1].bFMod=2;                       // --> freq channel
      }
    }
   else
    {
     spu.s_chan[ch].bFMod=0;                           // --> turn off fmod
     if(ch>0&&spu.s_chan[ch-1].bFMod==2)
      spu.s_chan[ch-1].bFMod=0;
    }
  }
}

////////////////////////////////////////////////////////////////////////
// NOISE register write
////////////////////////////////////////////////////////////////////////

static void NoiseOn(int start,int end,unsigned short val)
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   spu.s_chan[ch].bNoise=val&1;                        // -> noise on/off
  }
}

////////////////////////////////////////////////////////////////////////
// LEFT VOLUME register write
////////////////////////////////////////////////////////////////////////

// please note: sweep and phase invert are wrong... but I've never seen
// them used

static void SetVolumeL(unsigned char ch,short vol)     // LEFT VOLUME
{
 if (isTitleName(PARASITE_EVE_DISC_1_JP) && ch <= 1) {
 	if (SetDisableVolumeChange(vol)) {
		return;
	}
 }

 if(vol&0x8000)                                        // sweep?
  {
   short sInc=1;                                       // -> sweep up?
   if(vol&0x2000) sInc=-1;                             // -> or down?
   if(vol&0x1000) vol^=0xffff;                         // -> mmm... phase inverted? have to investigate this
   vol=((vol&0x7f)+1)/2;                               // -> sweep: 0..127 -> 0..64
   vol+=vol/(2*sInc);                                  // -> HACK: we don't sweep right now, so we just raise/lower the volume by the half!
   vol*=128;
  }
 else                                                  // no sweep:
  {
   if(vol&0x4000)                                      // -> mmm... phase inverted? have to investigate this
    //vol^=0xffff;
    vol=0x3fff-(vol&0x3fff);
  }

 vol&=0x3fff;
 spu.s_chan[ch].iLeftVolume=vol;                       // store volume
}

////////////////////////////////////////////////////////////////////////
// RIGHT VOLUME register write
////////////////////////////////////////////////////////////////////////

static void SetVolumeR(unsigned char ch,short vol)     // RIGHT VOLUME
{
 if (isTitleName(PARASITE_EVE_DISC_1_JP) && ch <= 1) {
 	if (SetDisableVolumeChange(vol)) {
		return;
	}
 }

 if(vol&0x8000)                                        // comments... see above :)
  {
   short sInc=1;
   if(vol&0x2000) sInc=-1;
   if(vol&0x1000) vol^=0xffff;
   vol=((vol&0x7f)+1)/2;        
   vol+=vol/(2*sInc);
   vol*=128;
  }
 else            
  {
   if(vol&0x4000) //vol=vol^=0xffff;
    vol=0x3fff-(vol&0x3fff);
  }

 vol&=0x3fff;

 spu.s_chan[ch].iRightVolume=vol;
}

////////////////////////////////////////////////////////////////////////
// PITCH register write
////////////////////////////////////////////////////////////////////////

static void SetPitch(int ch,unsigned short val)               // SET PITCH
{
 int NP;
 if(val>0x3fff) NP=0x3fff;                             // get pitch val
 else           NP=val;

 spu.s_chan[ch].iRawPitch=NP;
 spu.s_chan[ch].sinc=(NP<<4)|8;
 spu.s_chan[ch].sinc_inv=0;
 if (spu_config.iUseInterpolation == 1)
  spu.SB[ch * SB_SIZE + 32] = 1; // -> freq change in simple interpolation mode: set flag
}

////////////////////////////////////////////////////////////////////////
// REVERB register write
////////////////////////////////////////////////////////////////////////

static void ReverbOn(int start,int end,unsigned short val)
{
 int ch;

 for(ch=start;ch<end;ch++,val>>=1)                     // loop channels
  {
   spu.s_chan[ch].bReverb=val&1;                       // -> reverb on/off
  }
}

static int SetDisableVolumeChange(short vol) {
	static int DisableVolumeChange = 0;
	int disableVolume, enableVolume;

	if (isTitleName(PARASITE_EVE_DISC_1_JP)) {
		disableVolume = PARASITE_EVE_JP_DISABLE_VOLUME;
		enableVolume = PARASITE_EVE_JP_ENABLE_VOLUME;
	}

	if (vol == disableVolume) {
		DisableVolumeChange = 1;
	} else if (enableVolume <= vol) {
		DisableVolumeChange = 0;
	}

	return DisableVolumeChange;
}
