//---------------------------------------------------------------------------
// NEOPOP : Emulator as in Dreamland
//
// Copyright (c) 2001-2002 by neopop_uk
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version. See also the license.txt file for
//	additional informations.
//---------------------------------------------------------------------------

/*
//---------------------------------------------------------------------------
//=========================================================================

	TLCS900h_interpret.c

//=========================================================================
//---------------------------------------------------------------------------

  History of changes:
  ===================

20 JUL 2002 - neopop_uk
=======================================
- Cleaned and tidied up for the source release

26 JUL 2002 - neopop_uk
=======================================
- Fixed a nasty bug that only affects [src]"EX (mem), XWA", 
	it was executing "EX F,F'" instead - Very bad! 

28 JUL 2002 - neopop_uk
=======================================
- Added generic DIV and DIVS functions

30 AUG 2002 - neopop_uk
=======================================
- Fixed detection of R32+d16 addressing mode.

02 SEP 2002 - neopop_uk
=======================================
- Added the undocumented type 0x13 R32 address mode.

09 SEP 2002 - neopop_uk
=======================================
- Extra cycles_cpu_interpreter for addressing modes.

//---------------------------------------------------------------------------
*/

#include "TLCS900h_registers.h"
#include "../mem.h"
#include "../bios.h"
#include "TLCS900h_interpret.h"
#include "TLCS900h_interpret_single.h"
#include "TLCS900h_interpret_src.h"
#include "TLCS900h_interpret_dst.h"
#include "TLCS900h_interpret_reg.h"

#ifdef TLCS_ERRORS
static void DUMMY_instruction_error(const char* vaMessage,...) { }
void (*instruction_error)(const char* vaMessage,...) = DUMMY_instruction_error;
#endif

//=========================================================================

uint32_t	mem;		//Result of addressing mode
uint_fast8_t	size_cpu_interpreter = 0;		//operand size, 0 = Byte, 1 = Word, 2 = Long

uint8_t		first;		//The first byte
uint8_t		R;			//big R
uint8_t		second;		//The second opcode

bool	brCode;		//Register code used?
uint8_t		rCode;		//The code

int32_t		cycles_cpu_interpreter;		//How many state changes?
static int32_t		cycles_extra;	//How many extra state changes?

//=========================================================================

uint16_t fetch16(void)
{
	uint16_t a = loadW(pc);
	pc += 2;
	return a;
}

uint32_t fetch24(void)
{
	uint32_t b, a = loadW(pc);
	pc += 2;
	b = loadB(pc++);
	return (b << 16) | a;
}

uint32_t fetch32(void)
{
	uint32_t a = loadL(pc);
	pc += 4;
	return a;
}

//=============================================================================

void parityB(uint8_t value)
{
	uint8_t count = 0, i;

	for (i = 0; i < 8; i++)
	{
		if (value & 1) count++;
		value >>= 1;
	}

	// if (count & 1) == false, means even, thus SET
	SETFLAG_V((count & 1) == 0);
}

void parityW(uint16_t value)
{
	uint8_t count = 0, i;

	for (i = 0; i < 16; i++)
	{
		if (value & 1) count++;
		value >>= 1;
	}

	// if (count & 1) == false, means even, thus SET
	SETFLAG_V((count & 1) == 0);
}

//=========================================================================

void push8(uint8_t data)
{
   REGXSP -= 1;
   storeB(REGXSP, data);
}

void push16(uint16_t data)
{
   REGXSP -= 2;
   storeW(REGXSP, data);
}

void push32(uint32_t data)
{
   REGXSP -= 4;
   storeL(REGXSP, data);
}

uint8_t pop8(void)
{
   uint8_t temp = loadB(REGXSP);
   REGXSP += 1;
   return temp;
}

uint16_t pop16(void)
{
   uint16_t temp = loadW(REGXSP);
   REGXSP += 2;
   return temp;
}

uint32_t pop32(void)
{
   uint32_t temp = loadL(REGXSP);
   REGXSP += 4;
   return temp;
}

//=============================================================================

uint16_t generic_DIV_B(uint16_t val, uint8_t div)
{
	if (div == 0)
	{ 
		SETFLAG_V1
		return (val << 8) | ((val >> 8) ^ 0xFF);
	}
	else
	{
		uint16_t quo = val / (uint16_t)div;
		uint16_t rem = val % (uint16_t)div;
		if (quo > 0xFF) SETFLAG_V1 else SETFLAG_V0
		return (quo & 0xFF) | ((rem & 0xFF) << 8);
	}
}

uint32_t generic_DIV_W(uint32_t val, uint16_t div)
{
	if (div == 0)
	{ 
		SETFLAG_V1
		return (val << 16) | ((val >> 16) ^ 0xFFFF);
	}
	else
	{
		uint32_t quo = val / (uint32_t)div;
		uint32_t rem = val % (uint32_t)div;
		if (quo > 0xFFFF) SETFLAG_V1 else SETFLAG_V0
		return (quo & 0xFFFF) | ((rem & 0xFFFF) << 16);
	}
}

//=============================================================================

uint16_t generic_DIVS_B(int16 val, int8 div)
{
	if (div == 0)
	{
		SETFLAG_V1
		return (val << 8) | ((val >> 8) ^ 0xFF);
	}
	else
	{
		int16 quo = val / (int16)div;
		int16 rem = val % (int16)div;
		if (quo > 0xFF) SETFLAG_V1 else SETFLAG_V0
		return (quo & 0xFF) | ((rem & 0xFF) << 8);
	}
}

uint32_t generic_DIVS_W(int32_t val, int16 div)
{
	if (div == 0)
	{
		SETFLAG_V1
		return (val << 16) | ((val >> 16) ^ 0xFFFF);
	}
	else
	{
		int32_t quo = val / (int32_t)div;
		int32_t rem = val % (int32_t)div;
		if (quo > 0xFFFF) SETFLAG_V1 else SETFLAG_V0
		return (quo & 0xFFFF) | ((rem & 0xFFFF) << 16);
	}
}

//=============================================================================

uint8_t generic_ADD_B(uint8_t dst, uint8_t src)
{
	uint8_t half = (dst & 0xF) + (src & 0xF);
	uint32_t resultC = (uint32_t)dst + (uint32_t)src;
	uint8_t result = (uint8_t)(resultC & 0xFF);

	SETFLAG_S(result & 0x80);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int8)dst >= 0) && ((int8)src >= 0) && ((int8)result < 0)) ||
		(((int8)dst < 0)  && ((int8)src < 0) && ((int8)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N0;
	SETFLAG_C(resultC > 0xFF);

	return result;
}

uint16_t generic_ADD_W(uint16_t dst, uint16_t src)
{
	uint16_t half = (dst & 0xF) + (src & 0xF);
	uint32_t resultC = (uint32_t)dst + (uint32_t)src;
	uint16_t result = (uint16_t)(resultC & 0xFFFF);

	SETFLAG_S(result & 0x8000);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int16)dst >= 0) && ((int16)src >= 0) && ((int16)result < 0)) ||
		(((int16)dst < 0)  && ((int16)src < 0) && ((int16)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N0;
	SETFLAG_C(resultC > 0xFFFF);

	return result;
}

uint32_t generic_ADD_L(uint32_t dst, uint32_t src)
{
	uint64_t resultC = (uint64_t)dst + (uint64_t)src;
	uint32_t result = (uint32_t)(resultC & 0xFFFFFFFF);

	SETFLAG_S(result & 0x80000000);
	SETFLAG_Z(result == 0);

	if ((((int32_t)dst >= 0) && ((int32_t)src >= 0) && ((int32_t)result < 0)) || 
		(((int32_t)dst < 0)  && ((int32_t)src < 0) && ((int32_t)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}
	
	SETFLAG_N0;
	SETFLAG_C(resultC > 0xFFFFFFFF);

	return result;
}

//=============================================================================

uint8_t generic_ADC_B(uint8_t dst, uint8_t src)
{
	uint8_t half = (dst & 0xF) + (src & 0xF) + FLAG_C;
	uint32_t resultC = (uint32_t)dst + (uint32_t)src + (uint32_t)FLAG_C;
	uint8_t result = (uint8_t)(resultC & 0xFF);

	SETFLAG_S(result & 0x80);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int8)dst >= 0) && ((int8)src >= 0) && ((int8)result < 0)) || 
		(((int8)dst < 0)  && ((int8)src < 0) && ((int8)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N0;
	SETFLAG_C(resultC > 0xFF);

	return result;
}

uint16_t generic_ADC_W(uint16_t dst, uint16_t src)
{
	uint16_t half = (dst & 0xF) + (src & 0xF) + FLAG_C;
	uint32_t resultC = (uint32_t)dst + (uint32_t)src + (uint32_t)FLAG_C;
	uint16_t result = (uint16_t)(resultC & 0xFFFF);

	SETFLAG_S(result & 0x8000);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int16)dst >= 0) && ((int16)src >= 0) && ((int16)result < 0)) || 
		(((int16)dst < 0)  && ((int16)src < 0) && ((int16)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N0;
	SETFLAG_C(resultC > 0xFFFF);

	return result;
}

uint32_t generic_ADC_L(uint32_t dst, uint32_t src)
{
	uint64_t resultC = (uint64_t)dst + (uint64_t)src + (uint64_t)FLAG_C;
	uint32_t result = (uint32_t)(resultC & 0xFFFFFFFF);

	SETFLAG_S(result & 0x80000000);
	SETFLAG_Z(result == 0);

	if ((((int32_t)dst >= 0) && ((int32_t)src >= 0) && ((int32_t)result < 0)) || 
		(((int32_t)dst < 0)  && ((int32_t)src < 0) && ((int32_t)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}
	
	SETFLAG_N0;
	SETFLAG_C(resultC > 0xFFFFFFFF);

	return result;
}

//=============================================================================

uint8_t generic_SUB_B(uint8_t dst, uint8_t src)
{
	uint8_t half = (dst & 0xF) - (src & 0xF);
	uint32_t resultC = (uint32_t)dst - (uint32_t)src;
	uint8_t result = (uint8_t)(resultC & 0xFF);

	SETFLAG_S(result & 0x80);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int8)dst >= 0) && ((int8)src < 0) && ((int8)result < 0)) ||
		(((int8)dst < 0) && ((int8)src >= 0) && ((int8)result >= 0)))
	{
      SETFLAG_V1
   }
   else
   {
      SETFLAG_V0
   }

	SETFLAG_N1;
	SETFLAG_C(resultC > 0xFF);

	return result;
}

uint16_t generic_SUB_W(uint16_t dst, uint16_t src)
{
	uint16_t half = (dst & 0xF) - (src & 0xF);
	uint32_t resultC = (uint32_t)dst - (uint32_t)src;
	uint16_t result = (uint16_t)(resultC & 0xFFFF);

	SETFLAG_S(result & 0x8000);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int16)dst >= 0) && ((int16)src < 0) && ((int16)result < 0)) ||
		(((int16)dst < 0) && ((int16)src >= 0) && ((int16)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N1;
	SETFLAG_C(resultC > 0xFFFF);

	return result;
}

uint32_t generic_SUB_L(uint32_t dst, uint32_t src)
{
	uint64_t resultC = (uint64_t)dst - (uint64_t)src;
	uint32_t result = (uint32_t)(resultC & 0xFFFFFFFF);

	SETFLAG_S(result & 0x80000000);
	SETFLAG_Z(result == 0);

	if ((((int32_t)dst >= 0) && ((int32_t)src < 0) && ((int32_t)result < 0)) ||
		(((int32_t)dst < 0) && ((int32_t)src >= 0) && ((int32_t)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}
	
	SETFLAG_N1;
	SETFLAG_C(resultC > 0xFFFFFFFF);

	return result;
}

//=============================================================================

uint8_t generic_SBC_B(uint8_t dst, uint8_t src)
{
	uint8_t half = (dst & 0xF) - (src & 0xF) - FLAG_C;
	uint32_t resultC = (uint32_t)dst - (uint32_t)src - (uint32_t)FLAG_C;
	uint8_t result = (uint8_t)(resultC & 0xFF);

	SETFLAG_S(result & 0x80);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int8)dst >= 0) && ((int8)src < 0) && ((int8)result < 0)) ||
		(((int8)dst < 0) && ((int8)src >= 0) && ((int8)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N1;
	SETFLAG_C(resultC > 0xFF);

	return result;
}

uint16_t generic_SBC_W(uint16_t dst, uint16_t src)
{
	uint16_t half = (dst & 0xF) - (src & 0xF) - FLAG_C;
	uint32_t resultC = (uint32_t)dst - (uint32_t)src - (uint32_t)FLAG_C;
	uint16_t result = (uint16_t)(resultC & 0xFFFF);

	SETFLAG_S(result & 0x8000);
	SETFLAG_Z(result == 0);
	SETFLAG_H(half > 0xF);

	if ((((int16)dst >= 0) && ((int16)src < 0) && ((int16)result < 0)) ||
		(((int16)dst < 0) && ((int16)src >= 0) && ((int16)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}

	SETFLAG_N1;
	SETFLAG_C(resultC > 0xFFFF);

	return result;
}

uint32_t generic_SBC_L(uint32_t dst, uint32_t src)
{
	uint64_t resultC = (uint64_t)dst - (uint64_t)src - (uint64_t)FLAG_C;
	uint32_t result = (uint32_t)(resultC & 0xFFFFFFFF);

	SETFLAG_S(result & 0x80000000);
	SETFLAG_Z(result == 0);

	if ((((int32_t)dst >= 0) && ((int32_t)src < 0) && ((int32_t)result < 0)) ||
		(((int32_t)dst < 0) && ((int32_t)src >= 0) && ((int32_t)result >= 0)))
	{SETFLAG_V1} else {SETFLAG_V0}
	
	SETFLAG_N1;
	SETFLAG_C(resultC > 0xFFFFFFFF);

	return result;
}

//=============================================================================

bool conditionCode(uint_fast8_t cc)
{
   switch(cc)
   {
      case 0:
         return 0;	//(F)
      case 1:
         if (FLAG_S ^ FLAG_V)
            return 1;
         return 0;	//(LT)
      case 2:
         if (FLAG_Z | (FLAG_S ^ FLAG_V))
            return 1;
         return 0;	//(LE)
      case 3:
         if (FLAG_C | FLAG_Z)
            return 1;
         return 0;	//(ULE)
      case 4:
         if (FLAG_V)
            return 1;
         return 0;	//(OV)
      case 5:
         if (FLAG_S)
            return 1;
         return 0;	//(MI)
      case 6:
         if (FLAG_Z)
            return 1;
         return 0;	//(Z)
      case 7:
         if (FLAG_C)
            return 1;
         return 0;	//(C)
      case 8:
         return 1;	//always True														
      case 9:
         if (FLAG_S ^ FLAG_V)
            return 0;
         return 1;	//(GE)
      case 10:
         if (FLAG_Z | (FLAG_S ^ FLAG_V))
            return 0;
         return 1;	//(GT)
      case 11:
         if (FLAG_C | FLAG_Z)
            return 0;
         return 1;	//(UGT)
      case 12:
         if (FLAG_V)
            return 0;
         return 1;	//(NOV)
      case 13:
         if (FLAG_S)
            return 0;
         return 1;	//(PL)
      case 14:
         if (FLAG_Z)
            return 0;
         return 1;	//(NZ)
      case 15:
         if (FLAG_C)
            return 0;
         return 1;	//(NC)
   }

#ifdef NEOPOP_DEBUG
   system_debug_message("Unknown Condition Code %d", cc);
#endif
   return false;
}

//=============================================================================

uint8_t get_rr_Target(void)
{
	uint8_t target = 0x80;

	if (size_cpu_interpreter == 0 && first == 0xC7)
		return rCode;

	//Create a regCode
	switch(first & 7)
	{
	case 0: if (size_cpu_interpreter == 1)	target = 0xE0;	break;
	case 1:	
		if (size_cpu_interpreter == 0)	target = 0xE0;
		if (size_cpu_interpreter == 1)	target = 0xE4;
		break;
	case 2: if (size_cpu_interpreter == 1)	target = 0xE8;	break;
	case 3:
		if (size_cpu_interpreter == 0)	target = 0xE4;
		if (size_cpu_interpreter == 1)	target = 0xEC;
		break;
	case 4: if (size_cpu_interpreter == 1)	target = 0xF0;	break;
	case 5:	
		if (size_cpu_interpreter == 0)	target = 0xE8;
		if (size_cpu_interpreter == 1)	target = 0xF4;
		break;
	case 6: if (size_cpu_interpreter == 1)	target = 0xF8;	break;
	case 7:
		if (size_cpu_interpreter == 0)	target = 0xEC;
		if (size_cpu_interpreter == 1)	target = 0xFC;
		break;
	}

	return target;
}

uint8_t get_RR_Target(void)
{
   uint8_t target = 0x80;

   //Create a regCode
   switch(second & 7)
   {
      case 0:
         if (size_cpu_interpreter == 1)
            target = 0xE0;
         break;
      case 1:	
         if (size_cpu_interpreter == 0)
            target = 0xE0;
         if (size_cpu_interpreter == 1)
            target = 0xE4;
         break;
      case 2:
         if (size_cpu_interpreter == 1)
            target = 0xE8;
         break;
      case 3:
         if (size_cpu_interpreter == 0)
            target = 0xE4;
         if (size_cpu_interpreter == 1)
            target = 0xEC;
         break;
      case 4:
         if (size_cpu_interpreter == 1)
            target = 0xF0;
         break;
      case 5:	
         if (size_cpu_interpreter == 0)
            target = 0xE8;
         if (size_cpu_interpreter == 1)
            target = 0xF4;
         break;
      case 6:
         if (size_cpu_interpreter == 1)
            target = 0xF8;
         break;
      case 7:
         if (size_cpu_interpreter == 0)
            target = 0xEC;
         if (size_cpu_interpreter == 1)
            target = 0xFC;
         break;
   }

   return target;
}

//=========================================================================

static void ExXWA()		{mem = regL(0);}
static void ExXBC()		{mem = regL(1);}
static void ExXDE()		{mem = regL(2);}
static void ExXHL()		{mem = regL(3);}
static void ExXIX()		{mem = regL(4);}
static void ExXIY()		{mem = regL(5);}
static void ExXIZ()		{mem = regL(6);}
static void ExXSP()		{mem = regL(7);}

static void ExXWAd()	{mem = regL(0) + (int8)FETCH8; cycles_extra = 2;}
static void ExXBCd()	{mem = regL(1) + (int8)FETCH8; cycles_extra = 2;}
static void ExXDEd()	{mem = regL(2) + (int8)FETCH8; cycles_extra = 2;}
static void ExXHLd()	{mem = regL(3) + (int8)FETCH8; cycles_extra = 2;}
static void ExXIXd()	{mem = regL(4) + (int8)FETCH8; cycles_extra = 2;}
static void ExXIYd()	{mem = regL(5) + (int8)FETCH8; cycles_extra = 2;}
static void ExXIZd()	{mem = regL(6) + (int8)FETCH8; cycles_extra = 2;}
static void ExXSPd()	{mem = regL(7) + (int8)FETCH8; cycles_extra = 2;}

static void Ex8(void)
{
   mem = FETCH8;
   cycles_extra = 2;
}

static void Ex16(void)
{
   mem = fetch16();
   cycles_extra = 2;
}

static void Ex24(void)
{
   mem = fetch24();
   cycles_extra = 3;
}

static void ExR32(void)
{
	uint8_t data = FETCH8;

	if (data == 0x03)
	{
		uint8_t rIndex, r32;
		r32 = FETCH8;		//r32
		rIndex = FETCH8;	//r8
		mem = rCodeL(r32) + (int8)rCodeB(rIndex);
		cycles_extra = 8;
		return;
	}

	if (data == 0x07)
	{
		uint8_t rIndex, r32;
		r32 = FETCH8;		//r32
		rIndex = FETCH8;	//r16
		mem = rCodeL(r32) + (int16)rCodeW(rIndex);
		cycles_extra = 8;
		return;
	}

	//Undocumented mode!
	if (data == 0x13)
	{
		int16 disp = fetch16();
		mem = pc + disp;
		cycles_extra = 8;	//Unconfirmed... doesn't make much difference
		return;
	}

	cycles_extra = 5;

   mem = rCodeL(data);
	if ((data & 3) == 1)
		mem += (int16)fetch16();
}

static void ExDec()
{
	uint8_t data = FETCH8;
	uint8_t r32 = data & 0xFC;

	cycles_extra = 3;

	switch(data & 3)
	{
	case 0:	rCodeL(r32) -= 1;	mem = rCodeL(r32);	break;
	case 1:	rCodeL(r32) -= 2;	mem = rCodeL(r32);	break;
	case 2:	rCodeL(r32) -= 4;	mem = rCodeL(r32);	break;
	}
}

static void ExInc()
{
   uint8_t data = FETCH8;
   uint8_t r32 = data & 0xFC;

   cycles_extra = 3;

   switch(data & 3)
   {
      case 0:
         mem = rCodeL(r32);
         rCodeL(r32) += 1;
         break;
      case 1:
         mem = rCodeL(r32);
         rCodeL(r32) += 2;
         break;
      case 2:
         mem = rCodeL(r32);
         rCodeL(r32) += 4;
         break;
   }
}

static void ExRC()
{
	brCode = true;
	rCode = FETCH8;
	cycles_extra = 1;
}

//=========================================================================

//Address Mode & Register Code
static void (*decodeExtra[256])() = 
{
/*0*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*1*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*2*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*3*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*4*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*5*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*6*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*7*/	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*8*/	ExXWA,	ExXBC,	ExXDE,	ExXHL,	ExXIX,	ExXIY,	ExXIZ,	ExXSP,
		ExXWAd,	ExXBCd,	ExXDEd,	ExXHLd,	ExXIXd,	ExXIYd,	ExXIZd,	ExXSPd,
/*9*/	ExXWA,	ExXBC,	ExXDE,	ExXHL,	ExXIX,	ExXIY,	ExXIZ,	ExXSP,
		ExXWAd,	ExXBCd,	ExXDEd,	ExXHLd,	ExXIXd,	ExXIYd,	ExXIZd,	ExXSPd,
/*A*/	ExXWA,	ExXBC,	ExXDE,	ExXHL,	ExXIX,	ExXIY,	ExXIZ,	ExXSP,
		ExXWAd,	ExXBCd,	ExXDEd,	ExXHLd,	ExXIXd,	ExXIYd,	ExXIZd,	ExXSPd,
/*B*/	ExXWA,	ExXBC,	ExXDE,	ExXHL,	ExXIX,	ExXIY,	ExXIZ,	ExXSP,
		ExXWAd,	ExXBCd,	ExXDEd,	ExXHLd,	ExXIXd,	ExXIYd,	ExXIZd,	ExXSPd,
/*C*/	Ex8,	Ex16,	Ex24,	ExR32,	ExDec,	ExInc,	0,		ExRC,
		0,		0,		0,		0,		0,		0,		0,		0,
/*D*/	Ex8,	Ex16,	Ex24,	ExR32,	ExDec,	ExInc,	0,		ExRC,
		0,		0,		0,		0,		0,		0,		0,		0,
/*E*/	Ex8,	Ex16,	Ex24,	ExR32,	ExDec,	ExInc,	0,		ExRC,
		0,		0,		0,		0,		0,		0,		0,		0,
/*F*/	Ex8,	Ex16,	Ex24,	ExR32,	ExDec,	ExInc,	0,		0,
		0,		0,		0,		0,		0,		0,		0,		0
};

//=========================================================================

static void e(void)
{
	#ifdef TLCS_ERRORS
		instruction_error("Unknown instruction %02X", first);
	#endif
}

static void es(void)
{
	//instruction_error("Unknown [src] instruction %02X", second);
}

static void ed(void)
{
	//instruction_error("Unknown [dst] instruction %02X", second);
}

static void er(void)
{
	//instruction_error("Unknown [reg] instruction %02X", second);
}

//=========================================================================

//Secondary (SRC) Instruction decode
static void (*srcDecode[256])() = 
{
/*0*/	es,			es,			es,			es,			srcPUSH,	es,			srcRLD,		srcRRD,
		es,			es,			es,			es,			es,			es,			es,			es,
/*1*/	srcLDI,		srcLDIR,	srcLDD,		srcLDDR,	srcCPI,		srcCPIR,	srcCPD,		srcCPDR,
		es,			srcLD16m,	es,			es,			es,			es,			es,			es,
/*2*/	srcLD,		srcLD,		srcLD,		srcLD,		srcLD,		srcLD,		srcLD,		srcLD,
		es,			es,			es,			es,			es,			es,			es,			es,
/*3*/	srcEX,		srcEX,		srcEX,		srcEX,		srcEX,		srcEX,		srcEX,		srcEX,
		srcADDi,	srcADCi,	srcSUBi,	srcSBCi,	srcANDi,	srcXORi,	srcORi,		srcCPi,
/*4*/	srcMUL,		srcMUL,		srcMUL,		srcMUL,		srcMUL,		srcMUL,		srcMUL,		srcMUL,
		srcMULS,	srcMULS,	srcMULS,	srcMULS,	srcMULS,	srcMULS,	srcMULS,	srcMULS,
/*5*/	srcDIV,		srcDIV,		srcDIV,		srcDIV,		srcDIV,		srcDIV,		srcDIV,		srcDIV,
		srcDIVS,	srcDIVS,	srcDIVS,	srcDIVS,	srcDIVS,	srcDIVS,	srcDIVS,	srcDIVS,
/*6*/	srcINC,		srcINC,		srcINC,		srcINC,		srcINC,		srcINC,		srcINC,		srcINC,
		srcDEC,		srcDEC,		srcDEC,		srcDEC,		srcDEC,		srcDEC,		srcDEC,		srcDEC,
/*7*/	es,			es,			es,			es,			es,			es,			es,			es,
		srcRLC,		srcRRC,		srcRL,		srcRR,		srcSLA,		srcSRA,		srcSLL,		srcSRL,
/*8*/	srcADDRm,	srcADDRm,	srcADDRm,	srcADDRm,	srcADDRm,	srcADDRm,	srcADDRm,	srcADDRm,
		srcADDmR,	srcADDmR,	srcADDmR,	srcADDmR,	srcADDmR,	srcADDmR,	srcADDmR,	srcADDmR,
/*9*/	srcADCRm,	srcADCRm,	srcADCRm,	srcADCRm,	srcADCRm,	srcADCRm,	srcADCRm,	srcADCRm,
		srcADCmR,	srcADCmR,	srcADCmR,	srcADCmR,	srcADCmR,	srcADCmR,	srcADCmR,	srcADCmR,
/*A*/	srcSUBRm,	srcSUBRm,	srcSUBRm,	srcSUBRm,	srcSUBRm,	srcSUBRm,	srcSUBRm,	srcSUBRm,
		srcSUBmR,	srcSUBmR,	srcSUBmR,	srcSUBmR,	srcSUBmR,	srcSUBmR,	srcSUBmR,	srcSUBmR,
/*B*/	srcSBCRm,	srcSBCRm,	srcSBCRm,	srcSBCRm,	srcSBCRm,	srcSBCRm,	srcSBCRm,	srcSBCRm,
		srcSBCmR,	srcSBCmR,	srcSBCmR,	srcSBCmR,	srcSBCmR,	srcSBCmR,	srcSBCmR,	srcSBCmR,
/*C*/	srcANDRm,	srcANDRm,	srcANDRm,	srcANDRm,	srcANDRm,	srcANDRm,	srcANDRm,	srcANDRm,
		srcANDmR,	srcANDmR,	srcANDmR,	srcANDmR,	srcANDmR,	srcANDmR,	srcANDmR,	srcANDmR,
/*D*/	srcXORRm,	srcXORRm,	srcXORRm,	srcXORRm,	srcXORRm,	srcXORRm,	srcXORRm,	srcXORRm,
		srcXORmR,	srcXORmR,	srcXORmR,	srcXORmR,	srcXORmR,	srcXORmR,	srcXORmR,	srcXORmR,
/*E*/	srcORRm,	srcORRm,	srcORRm,	srcORRm,	srcORRm,	srcORRm,	srcORRm,	srcORRm,
		srcORmR,	srcORmR,	srcORmR,	srcORmR,	srcORmR,	srcORmR,	srcORmR,	srcORmR,
/*F*/	srcCPRm,	srcCPRm,	srcCPRm,	srcCPRm,	srcCPRm,	srcCPRm,	srcCPRm,	srcCPRm,
		srcCPmR,	srcCPmR,	srcCPmR,	srcCPmR,	srcCPmR,	srcCPmR,	srcCPmR,	srcCPmR
};

//Secondary (DST) Instruction decode
static void (*dstDecode[256])() = 
{
/*0*/	DST_dstLDBi,	ed,			DST_dstLDWi,	ed,			DST_dstPOPB,	ed,			DST_dstPOPW,	ed,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*1*/	ed,			ed,			ed,			ed,			DST_dstLDBm16,	ed,			DST_dstLDWm16,	ed,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*2*/	DST_dstLDAW,	DST_dstLDAW,	DST_dstLDAW,	DST_dstLDAW,	DST_dstLDAW,	DST_dstLDAW,	DST_dstLDAW,	DST_dstLDAW,
		DST_dstANDCFA,	DST_dstORCFA,	DST_dstXORCFA,	DST_dstLDCFA,	DST_dstSTCFA,	ed,			ed,			ed,
/*3*/	DST_dstLDAL,	DST_dstLDAL,	DST_dstLDAL,	DST_dstLDAL,	DST_dstLDAL,	DST_dstLDAL,	DST_dstLDAL,	DST_dstLDAL,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*4*/	DST_dstLDBR,	DST_dstLDBR,	DST_dstLDBR,	DST_dstLDBR,	DST_dstLDBR,	DST_dstLDBR,	DST_dstLDBR,	DST_dstLDBR,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*5*/	DST_dstLDWR,	DST_dstLDWR,	DST_dstLDWR,	DST_dstLDWR,	DST_dstLDWR,	DST_dstLDWR,	DST_dstLDWR,	DST_dstLDWR,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*6*/	DST_dstLDLR,	DST_dstLDLR,	DST_dstLDLR,	DST_dstLDLR,	DST_dstLDLR,	DST_dstLDLR,	DST_dstLDLR,	DST_dstLDLR,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*7*/	ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
		ed,			ed,			ed,			ed,			ed,			ed,			ed,			ed,
/*8*/	DST_dstANDCF,	DST_dstANDCF,	DST_dstANDCF,	DST_dstANDCF,	DST_dstANDCF,	DST_dstANDCF,	DST_dstANDCF,	DST_dstANDCF,
		DST_dstORCF,	DST_dstORCF,	DST_dstORCF,	DST_dstORCF,	DST_dstORCF,	DST_dstORCF,	DST_dstORCF,	DST_dstORCF,
/*9*/	DST_dstXORCF,	DST_dstXORCF,	DST_dstXORCF,	DST_dstXORCF,	DST_dstXORCF,	DST_dstXORCF,	DST_dstXORCF,	DST_dstXORCF,
		DST_dstLDCF,	DST_dstLDCF,	DST_dstLDCF,	DST_dstLDCF,	DST_dstLDCF,	DST_dstLDCF,	DST_dstLDCF,	DST_dstLDCF,
/*A*/	DST_dstSTCF,	DST_dstSTCF,	DST_dstSTCF,	DST_dstSTCF,	DST_dstSTCF,	DST_dstSTCF,	DST_dstSTCF,	DST_dstSTCF,	
		DST_dstTSET,	DST_dstTSET,	DST_dstTSET,	DST_dstTSET,	DST_dstTSET,	DST_dstTSET,	DST_dstTSET,	DST_dstTSET,
/*B*/	DST_dstRES,		DST_dstRES,		DST_dstRES,		DST_dstRES,		DST_dstRES,		DST_dstRES,		DST_dstRES,		DST_dstRES,
		DST_dstSET,		DST_dstSET,		DST_dstSET,		DST_dstSET,		DST_dstSET,		DST_dstSET,		DST_dstSET,		DST_dstSET,
/*C*/	DST_dstCHG,		DST_dstCHG,		DST_dstCHG,		DST_dstCHG,		DST_dstCHG,		DST_dstCHG,		DST_dstCHG,		DST_dstCHG,
		DST_dstBIT,		DST_dstBIT,		DST_dstBIT,		DST_dstBIT,		DST_dstBIT,		DST_dstBIT,		DST_dstBIT,		DST_dstBIT,
/*D*/	DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,
		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,		DST_dstJP,
/*E*/	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,
		DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,	DST_dstCALL,
/*F*/	DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,
		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET,		DST_dstRET
};

//Secondary (REG) Instruction decode
static void (*regDecode[256])() = 
{
/*0*/	er,			er,			er,			regLDi,		regPUSH,	regPOP,		regCPL,		regNEG,
		regMULi,	regMULSi,	regDIVi,	regDIVSi,	regLINK,	regUNLK,	regBS1F,	regBS1B,
/*1*/	regDAA,		er,			regEXTZ,	regEXTS,	regPAA,		er,			regMIRR,	er,
		er,			regMULA,	er,			er,			regDJNZ,	er,			er,			er,
/*2*/	regANDCFi,	regORCFi,	regXORCFi,	regLDCFi,	regSTCFi,	er,			er,			er,
		regANDCFA,	regORCFA,	regXORCFA,	regLDCFA,	regSTCFA,	er,			regLDCcrr,	regLDCrcr,
/*3*/	regRES,		regSET,		regCHG,		regBIT,		regTSET,	er,			er,			er,
		regMINC1,	regMINC2,	regMINC4,	er,			regMDEC1,	regMDEC2,	regMDEC4,	er,
/*4*/	regMUL,		regMUL,		regMUL,		regMUL,		regMUL,		regMUL,		regMUL,		regMUL,
		regMULS,	regMULS,	regMULS,	regMULS,	regMULS,	regMULS,	regMULS,	regMULS,
/*5*/	regDIV,		regDIV,		regDIV,		regDIV,		regDIV,		regDIV,		regDIV,		regDIV,
		regDIVS,	regDIVS,	regDIVS,	regDIVS,	regDIVS,	regDIVS,	regDIVS,	regDIVS,
/*6*/	regINC,		regINC,		regINC,		regINC,		regINC,		regINC,		regINC,		regINC,
		regDEC,		regDEC,		regDEC,		regDEC,		regDEC,		regDEC,		regDEC,		regDEC,
/*7*/	regSCC,		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,
		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,		regSCC,
/*8*/	regADD,		regADD,		regADD,		regADD,		regADD,		regADD,		regADD,		regADD,
		regLDRr,	regLDRr,	regLDRr,	regLDRr,	regLDRr,	regLDRr,	regLDRr,	regLDRr,
/*9*/	regADC,		regADC,		regADC,		regADC,		regADC,		regADC,		regADC,		regADC,
		regLDrR,	regLDrR,	regLDrR,	regLDrR,	regLDrR,	regLDrR,	regLDrR,	regLDrR,
/*A*/	regSUB,		regSUB,		regSUB,		regSUB,		regSUB,		regSUB,		regSUB,		regSUB,
		regLDr3,	regLDr3,	regLDr3,	regLDr3,	regLDr3,	regLDr3,	regLDr3,	regLDr3,
/*B*/	regSBC,		regSBC,		regSBC,		regSBC,		regSBC,		regSBC,		regSBC,		regSBC,
		regEX,		regEX,		regEX,		regEX,		regEX,		regEX,		regEX,		regEX,
/*C*/	regAND,		regAND,		regAND,		regAND,		regAND,		regAND,		regAND,		regAND,
		regADDi,	regADCi,	regSUBi,	regSBCi,	regANDi,	regXORi,	regORi,		regCPi,
/*D*/	regXOR,		regXOR,		regXOR,		regXOR,		regXOR,		regXOR,		regXOR,		regXOR,
		regCPr3,	regCPr3,	regCPr3,	regCPr3,	regCPr3,	regCPr3,	regCPr3,	regCPr3,
/*E*/	regOR,		regOR,		regOR,		regOR,		regOR,		regOR,		regOR,		regOR,
		regRLCi,	regRRCi,	regRLi,		regRRi,		regSLAi,	regSRAi,	regSLLi,	regSRLi,
/*F*/	regCP,		regCP,		regCP,		regCP,		regCP,		regCP,		regCP,		regCP,
		regRLCA,	regRRCA,	regRLA,		regRRA,		regSLAA,	regSRAA,	regSLLA,	regSRLA
};

//=========================================================================

static void src_B()
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;
	size_cpu_interpreter = 0;					//Byte Size

	(*srcDecode[second])();		//Call
}

static void src_W()
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;
	size_cpu_interpreter = 1;					//Word Size

	(*srcDecode[second])();		//Call
}

static void src_L()
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;
	size_cpu_interpreter = 2;					//Long Size

	(*srcDecode[second])();		//Call
}

static void dst()
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;

	(*dstDecode[second])();		//Call
}

static uint8_t rCodeConversionB[8] = { 0xE1, 0xE0, 0xE5, 0xE4, 0xE9, 0xE8, 0xED, 0xEC };
static uint8_t rCodeConversionW[8] = { 0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC };
static uint8_t rCodeConversionL[8] = { 0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC };

static void reg_B(void)
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;
	size_cpu_interpreter = 0;					//Byte Size

	if (brCode == false)
	{
		brCode = true;
		rCode = rCodeConversionB[first & 7];
	}

	(*regDecode[second])();		//Call
}

static void reg_W(void)
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;
	size_cpu_interpreter = 1;					//Word Size

	if (brCode == false)
	{
		brCode = true;
		rCode = rCodeConversionW[first & 7];
	}

	(*regDecode[second])();		//Call
}

static void reg_L(void)
{
	second = FETCH8;			//Get the second opcode
	R = second & 7;
	size_cpu_interpreter = 2;					//Long Size

	if (brCode == false)
	{
		brCode = true;
		rCode = rCodeConversionL[first & 7];
	}

	(*regDecode[second])();		//Call
}

//=============================================================================

//Primary Instruction decode
static void (*decode[256])() = 
{
/*0*/	sngNOP,		sngNORMAL,	sngPUSHSR,	sngPOPSR,	sngMAX,		sngHALT,	sngEI,		sngRETI,
		sngLD8_8,	sngPUSH8,	sngLD8_16,	sngPUSH16,	sngINCF,	sngDECF,	sngRET,		sngRETD,
/*1*/	sngRCF,		sngSCF,		sngCCF,		sngZCF,		sngPUSHA,	sngPOPA,	sngEX,		sngLDF,
		sngPUSHF,	sngPOPF,	sngJP16,	sngJP24,	sngCALL16,	sngCALL24,	sngCALR,	iBIOSHLE,
/*2*/	sngLDB,		sngLDB,		sngLDB,		sngLDB,		sngLDB,		sngLDB,		sngLDB,		sngLDB,
		sngPUSHW,	sngPUSHW,	sngPUSHW,	sngPUSHW,	sngPUSHW,	sngPUSHW,	sngPUSHW,	sngPUSHW,
/*3*/	sngLDW,		sngLDW,		sngLDW,		sngLDW,		sngLDW,		sngLDW,		sngLDW,		sngLDW,
		sngPUSHL,	sngPUSHL,	sngPUSHL,	sngPUSHL,	sngPUSHL,	sngPUSHL,	sngPUSHL,	sngPUSHL,
/*4*/	sngLDL,		sngLDL,		sngLDL,		sngLDL,		sngLDL,		sngLDL,		sngLDL,		sngLDL,
		sngPOPW,	sngPOPW,	sngPOPW,	sngPOPW,	sngPOPW,	sngPOPW,	sngPOPW,	sngPOPW,
/*5*/	e,			e,			e,			e,			e,			e,			e,			e,
		sngPOPL,	sngPOPL,	sngPOPL,	sngPOPL,	sngPOPL,	sngPOPL,	sngPOPL,	sngPOPL,
/*6*/	sngJR,		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,
		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,		sngJR,
/*7*/	sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,
		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,		sngJRL,
/*8*/	src_B,		src_B,		src_B,		src_B,		src_B,		src_B,		src_B,		src_B,
		src_B,		src_B,		src_B,		src_B,		src_B,		src_B,		src_B,		src_B,
/*9*/	src_W,		src_W,		src_W,		src_W,		src_W,		src_W,		src_W,		src_W,
		src_W,		src_W,		src_W,		src_W,		src_W,		src_W,		src_W,		src_W,
/*A*/	src_L,		src_L,		src_L,		src_L,		src_L,		src_L,		src_L,		src_L,
		src_L,		src_L,		src_L,		src_L,		src_L,		src_L,		src_L,		src_L,
/*B*/	dst,		dst,		dst,		dst,		dst,		dst,		dst,		dst,
		dst,		dst,		dst,		dst,		dst,		dst,		dst,		dst,
/*C*/	src_B,		src_B,		src_B,		src_B,		src_B,		src_B,		e,			reg_B,
		reg_B,		reg_B,		reg_B,		reg_B,		reg_B,		reg_B,		reg_B,		reg_B,
/*D*/	src_W,		src_W,		src_W,		src_W,		src_W,		src_W,		e,			reg_W,
		reg_W,		reg_W,		reg_W,		reg_W,		reg_W,		reg_W,		reg_W,		reg_W,
/*E*/	src_L,		src_L,		src_L,		src_L,		src_L,		src_L,		e,			reg_L,
		reg_L,		reg_L,		reg_L,		reg_L,		reg_L,		reg_L,		reg_L,		reg_L,
/*F*/	dst,		dst,		dst,		dst,		dst,		dst,		e,			sngLDX,
		sngSWI,		sngSWI,		sngSWI,		sngSWI,		sngSWI,		sngSWI,		sngSWI,		sngSWI
};

//=============================================================================

int32_t TLCS900h_interpret(void)
{
	brCode = false;

	first = FETCH8;	//Get the first byte

	//Is any extra data used by this instruction?
	cycles_extra = 0;
	if (decodeExtra[first])
		(*decodeExtra[first])();

	(*decode[first])();	//Decode

	return cycles_cpu_interpreter + cycles_extra;
}

//=============================================================================
