/****************************************************************************/
/*
Copyright 2015 by Joseph Forgione
This file is part of VCC (Virtual Color Computer).

    VCC (Virtual Color Computer) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    VCC (Virtual Color Computer) is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VCC (Virtual Color Computer).  If not, see <http://www.gnu.org/licenses/>.
*/
/****************************************************************************/
/*
	The CoCo Program Pak - ROM Pak or plug-in DLL device
*/
/****************************************************************************/

#include "pakinterface.h"
#include "defines.h"
#include "tcc1014mmu.h"
#include "config.h"
#include "Vcc.h"
#include "mc6821.h"
#include "logger.h"
#include "fileops.h"

#include <commdlg.h>
#include <stdio.h>
#include <process.h>

/****************************************************************************/

// Storage for Pak ROMs
static uint8_t *		ExternalRomBuffer = nullptr; 
static bool				RomPackLoaded = false;
static unsigned int		BankedCartOffset=0;

static bool DialogOpen=false;

static char Did=0;

typedef struct {
	char MenuName[512];
	int MenuId;
	int Type;
} Dmenu;

static Dmenu MenuItem[100];
static unsigned char MenuIndex=0;
static HMENU hMenu = NULL;
static HMENU hSubMenu[64] ;

/** the loaded Pak object - ROM or plug-in DLL */
static vccpak_t		g_Pak = {
	// init blank / empty
	NULL,
	"",
	"Blank",
	0,
	{ NULL } 
};	

/** Last path used opening any Pak (ROM or DLL) */
char LastPakPath[MAX_PATH] = "";

/****************************************************************************/
/** 
	detect the type of file the user is trying to load 
*/
int FileID(char *Filename)
{
	FILE *DummyHandle = NULL;
	char Temp[3] = "";
	DummyHandle = fopen(Filename, "rb");
	if (DummyHandle == NULL)
	{
		return(0);	//File Doesn't exist
	}

	Temp[0] = fgetc(DummyHandle);
	Temp[1] = fgetc(DummyHandle);
	Temp[2] = 0;
	fclose(DummyHandle);
	if (strcmp(Temp, "MZ") == 0)
		return(1);	//DLL File

	return(2);		//Rom Image 
}

/****************************************************************************/
/****************************************************************************/
/**
*/
void vccPakSetLastPath(char * pLastPakPath)
{
	strcpy(LastPakPath, pLastPakPath);
}

/**
*/
char * vccPakGetLastPath()
{
	return LastPakPath;
}

/****************************************************************************/
/**
*/
void vccPakTimer(void)
{
	if ( g_Pak.api.heartbeat != NULL )
	{
		(*g_Pak.api.heartbeat)();
	}
}

/****************************************************************************/
/**
*/
void vccPakReset(void)
{
	BankedCartOffset=0;
	if (g_Pak.api.reset != NULL)
	{
		(*g_Pak.api.reset)();
	}
}

/****************************************************************************/
/**
*/
void vccPakGetStatus(char * pStatusBuffer)
{
	if (g_Pak.api.status != NULL)
	{
		(*g_Pak.api.status)(pStatusBuffer);
	}
	else
	{
		sprintf(pStatusBuffer, "");
	}
}

/****************************************************************************/
/**
*/
unsigned char vccPakPortRead (unsigned char port)
{
	if (g_Pak.api.portRead != NULL)
	{
		return (*g_Pak.api.portRead)(port);
	}

	return 0;
}

/****************************************************************************/
/**
*/
void vccPakPortWrite(unsigned char Port,unsigned char Data)
{
	if (g_Pak.api.portWrite != NULL)
	{
		(*g_Pak.api.portWrite)(Port,Data);
	}
	
	if ((Port == 0x40) && (RomPackLoaded == true)) 
	{
		BankedCartOffset = (Data & 15) << 14;
	}
}

/****************************************************************************/
/**
*/
unsigned char vccPakMem8Read (unsigned short Address)
{
	if (g_Pak.api.memRead != NULL)
	{
		return (*g_Pak.api.memRead)(Address & 32767);
	}

	if ( ExternalRomBuffer != NULL )
	{
		return (ExternalRomBuffer[(Address & 32767) + BankedCartOffset]);
	}
	
	return(0);
}

/****************************************************************************/
/**
*/
void vccPakMem8Write(unsigned char Port,unsigned char Data)
{
	return;
}

/****************************************************************************/
/**
*/
unsigned short vccPackGetAudioSample(void)
{
	if (g_Pak.api.getSample != NULL)
	{
		return(*g_Pak.api.getSample)();
	}
	
	return NULL;
}

/****************************************************************************/
/**
*/
void vccPakSetInterruptCallPtr(void)
{
	if (g_Pak.api.setInterruptCallPtr != NULL)
	{
		(*g_Pak.api.setInterruptCallPtr)(CPUAssertInterupt);
	}
}

/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/**
*/
int LoadCart(void)
{
	OPENFILENAME ofn ;	
	char szFileName[MAX_PATH]="";
	BOOL result;

	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize       = sizeof (OPENFILENAME) ;
	ofn.hwndOwner         = EmuState.WindowHandle;
	ofn.lpstrFilter       = "Program Packs\0*.ROM;*.BIN;*.DLL\0\0";			// filter string
	ofn.nFilterIndex      = 1 ;							// current filter index
	ofn.lpstrFile         = szFileName ;				// contains full path and filename on return
	ofn.nMaxFile          = MAX_PATH;					// sizeof lpstrFile
	ofn.lpstrFileTitle    = NULL;						// filename and extension only
	ofn.nMaxFileTitle     = MAX_PATH ;					// sizeof lpstrFileTitle
	ofn.lpstrTitle        = TEXT("Load Program Pack") ;	// title bar string
	ofn.Flags             = OFN_HIDEREADONLY;
	ofn.lpstrInitialDir = NULL;						// initial directory
	if (strlen(LastPakPath) > 0)
	{
		ofn.lpstrInitialDir = LastPakPath;
	}

	result = GetOpenFileName(&ofn);
	if (result)
	{
		// save last path
		strcpy(LastPakPath, szFileName);
		PathRemoveFileSpec(LastPakPath);

		if (!InsertModule(szFileName))
		{
			return(0);
		}
	}

	return(1);
}

/****************************************************************************/
/**
*/
void vccPakSetParamFlags(vccpak_t * pPak)
{
	char String[1024] = "";
	char TempIni[MAX_PATH] = "";

	strcat(String, "Module Name: ");
	strcat(String, g_Pak.name);
	strcat(String, "\n");

	if (g_Pak.api.config != NULL)
	{
		g_Pak.params |= VCCPAK_HASCONFIG;

		strcat(String, "Has Configurable options\n");
	}
	if (g_Pak.api.portWrite != NULL)
	{
		g_Pak.params |= VCCPAK_HASIOWRITE;

		strcat(String, "Is IO writable\n");
	}
	if (g_Pak.api.portRead != NULL)
	{
		g_Pak.params |= VCCPAK_HASIOREAD;

		strcat(String, "Is IO readable\n");
	}
	if (g_Pak.api.setInterruptCallPtr != NULL)
	{
		g_Pak.params |= VCCPAK_NEEDSCPUIRQ;

		strcat(String, "Generates Interupts\n");
	}
	if (g_Pak.api.memPointers != NULL)
	{
		g_Pak.params |= VCCPAK_DOESDMA;

		strcat(String, "Generates DMA Requests\n");
	}
	if (g_Pak.api.heartbeat != NULL)
	{
		g_Pak.params |= VCCPAK_NEEDHEARTBEAT;

		strcat(String, "Needs Heartbeat\n");
	}
	if (g_Pak.api.getSample != NULL)
	{
		g_Pak.params |= VCCPAK_ANALOGAUDIO;

		strcat(String, "Analog Audio Outputs\n");
	}
	if (g_Pak.api.memWrite != NULL)
	{
		g_Pak.params |= VCCPAK_CSWRITE;

		strcat(String, "Needs ChipSelect Write\n");
	}
	if (g_Pak.api.memRead != NULL)
	{
		g_Pak.params |= VCCPAK_CSREAD;

		strcat(String, "Needs ChipSelect Read\n");
	}
	if (g_Pak.api.status != NULL)
	{
		g_Pak.params |= VCCPAK_RETURNSSTATUS;

		strcat(String, "Returns Status\n");
	}
	if (g_Pak.api.reset != NULL)
	{
		g_Pak.params |= VCCPAK_CARTRESET;

		strcat(String, "Needs Reset Notification\n");
	}
	if (g_Pak.api.setINIPath != NULL)
	{
		g_Pak.params |= VCCPAK_SAVESINI;

		GetIniFilePath(TempIni);
		(*g_Pak.api.setINIPath)(TempIni);
	}
	if (g_Pak.api.setCartPtr != NULL)
	{
		g_Pak.params |= VCCPAK_ASSERTCART;

		strcat(String, "Can Assert CART\n");

		(*g_Pak.api.setCartPtr)(SetCart);
	}
}

/****************************************************************************/
/**
*/
int InsertModule (char *ModulePath)
{
//	char Modname[MAX_LOADSTRING]="Blank";
	char CatNumber[MAX_LOADSTRING]="";
	char Temp[MAX_LOADSTRING]="";
	unsigned char FileType=0;

	FileType = FileID(ModulePath);

	switch (FileType)
	{
	case 0:		//File doesn't exist
		return(NOMODULE);
		break;

	case 2:		//File is a ROM image

		UnloadDll();
		load_ext_rom(ModulePath);
		strncpy(g_Pak.name,ModulePath,MAX_PATH);
		PathStripPath(g_Pak.name);
		DynamicMenuCallback( "",0, 0); //Refresh Menus
		DynamicMenuCallback( "",1, 0);
		EmuState.ResetPending=2;
		SetCart(1);
		return(NOMODULE);
	break;

	case 1:		//File is a DLL
		UnloadDll();
		g_Pak.hDLib = LoadLibrary(ModulePath);
		if (g_Pak.hDLib == NULL)
		{
			return(NOMODULE);
		}

		SetCart(0);

		/*
			
		*/
		g_Pak.api.getName		= (vccpakapi_getname_t)		GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_GETNAME);
		g_Pak.api.config		= (vccpakapi_config_t)		GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_CONFIG);
		g_Pak.api.portWrite		= (vccpakapi_portwrite_t)	GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_PORTWRITE);
		g_Pak.api.portRead		= (vccpakapi_portread_t)	GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_PORTREAD);
		g_Pak.api.setInterruptCallPtr = (vccpakapi_setintptr_t)GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_ASSERTINTERRUPT);
		g_Pak.api.memPointers	= (vccpakapi_setmemptrs_t)	GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_MEMPOINTERS);
		g_Pak.api.heartbeat		= (vccpakapi_heartbeat_t)	GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_HEARTBEAT);
		g_Pak.api.memWrite		= (vcccpu_write8_t)			GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_MEMWRITE);
		g_Pak.api.memRead		= (vcccpu_read8_t) 			GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_MEMREAD);
		g_Pak.api.status		= (vccpakapi_status_t)		GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_STATUS);
		g_Pak.api.getSample		= (vccpakapi_getaudiosample_t) GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_AUDIOSAMPLE);
		g_Pak.api.reset			= (vccpakapi_reset_t)		GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_RESET);
		g_Pak.api.setINIPath	= (vccpakapi_setinipath_t)	GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_SETINIPATH);
		g_Pak.api.setCartPtr	= (vccpakapi_setcartptr_t)	GetProcAddress((HINSTANCE)g_Pak.hDLib, VCC_PAKAPI_SETCART);

		if (g_Pak.api.getName == NULL)
		{
			FreeLibrary((HINSTANCE)g_Pak.hDLib);
			g_Pak.hDLib =NULL;
			return(NOTVCC);
		}

		BankedCartOffset=0;

		//
		// Initialize pak
		//
		if (g_Pak.api.memPointers != NULL)
		{
			// pass in our memory read/write functions
			(*g_Pak.api.memPointers)(MemRead8, MemWrite8);
		}
		if (g_Pak.api.setInterruptCallPtr != NULL)
		{
			// pass in our assert interrrupt function
			(*g_Pak.api.setInterruptCallPtr)(CPUAssertInterupt);
		}
		// initialize / start dynamic menu
		(*g_Pak.api.getName)(g_Pak.name, CatNumber, DynamicMenuCallback, EmuState.WindowHandle);

		sprintf(Temp,"Configure %s", g_Pak.name);
		
		vccPakSetParamFlags(&g_Pak);

		strcpy(g_Pak.path,ModulePath);

		EmuState.ResetPending=2;
		
		return(0);
		break;
	}

	return(NOMODULE);
}

/****************************************************************************/
/**
	Load a ROM pack
	
	@return total bytes loaded, or 0 on failure
*/
int load_ext_rom(char filename[MAX_PATH])
{
	constexpr size_t PAK_MAX_MEM = 0x40000;

	// If there is an existing ROM, ditch it
	if (ExternalRomBuffer != nullptr) {
		free(ExternalRomBuffer);
	}
	
	// Allocate memory for the ROM
	ExternalRomBuffer = (uint8_t*)malloc(PAK_MAX_MEM);

	// If memory was unable to be allocated, fail
	if (ExternalRomBuffer == nullptr) {
		MessageBox(0, "cant allocate ram", "Ok", 0);
		return 0;
	}
	
	// Open the ROM file, fail if unable to
	FILE *rom_handle = fopen(filename, "rb");
	if (rom_handle == nullptr) return 0;
	
	// Load the file, one byte at a time.. (TODO: Get size and read entire block)
	size_t index=0;
	while ((feof(rom_handle) == 0) && (index < PAK_MAX_MEM)) {
		ExternalRomBuffer[index++] = fgetc(rom_handle);
	}
	fclose(rom_handle);
	
	UnloadDll();
	BankedCartOffset=0;
	RomPackLoaded=true;
	
	return index;
}

/****************************************************************************/
/**
*/
void UnloadDll(void)
{
	if ((DialogOpen==true) & (EmuState.EmulationRunning==1))
	{
		MessageBox(0,"Close Configuration Dialog before unloading","Ok",0);
		return;
	}

	// clear Pak API calls
	memset(&g_Pak.api, 0, sizeof(vccpakapi_t));

	if (g_Pak.hDLib != NULL)
	{
		FreeLibrary((HINSTANCE)g_Pak.hDLib);
	}
	g_Pak.hDLib =NULL;
	
	DynamicMenuCallback( "",0, 0); //Refresh Menus
	DynamicMenuCallback( "",1, 0);
//	DynamicMenuCallback("",0,0);
}

/****************************************************************************/
/**
*/
void GetCurrentModule(char * DefaultModule)
{
	strcpy(DefaultModule, g_Pak.path);
}

/****************************************************************************/
/**
*/
void UnloadPack(void)
{
	UnloadDll();
	strcpy(g_Pak.path,"");
	strcpy(g_Pak.name,"Blank");
	memcpy(&g_Pak.api, 0, sizeof(vccpakapi_t));
	RomPackLoaded=false;
	SetCart(0);
	
	if (ExternalRomBuffer != nullptr) 
	{
		free(ExternalRomBuffer);
	}
	ExternalRomBuffer=nullptr;

	EmuState.ResetPending=2;
	DynamicMenuCallback( "",0, 0); //Refresh Menus
	DynamicMenuCallback( "",1, 0);
}

/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/**
*/
void DynamicMenuActivated(unsigned char MenuItem)
{
	switch (MenuItem)
	{
	case 1:
		LoadPack();
		break;
	case 2:
		UnloadPack();
		break;
	default:
		if (g_Pak.api.config != NULL)
		{
			(*g_Pak.api.config)(MenuItem);
		}
		break;
	}
}

/****************************************************************************/

void DynamicMenuCallback( char *MenuName,int MenuId, int Type)
{
	char Temp[256]="";
	//MenuId=0 Flush Buffer MenuId=1 Done 
	switch (MenuId)
	{
		case 0:
			MenuIndex=0;
			DynamicMenuCallback( "Cartridge",6000,HEAD);	//Recursion is fun
			DynamicMenuCallback( "Load Cart",5001,SLAVE);
			sprintf(Temp,"Eject Cart: ");
			strcat(Temp, g_Pak.name);
			DynamicMenuCallback( Temp,5002,SLAVE);
		break;

		case 1:
			RefreshDynamicMenu();
		break;

		default:
			strcpy(MenuItem[MenuIndex].MenuName,MenuName);
			MenuItem[MenuIndex].MenuId=MenuId;
			MenuItem[MenuIndex].Type=Type;
			MenuIndex++;
		break;	
	}
}

/****************************************************************************/

void RefreshDynamicMenu(void)
{
	MENUITEMINFO	Mii;
	char MenuTitle[32]="Cartridge";
	unsigned char TempIndex=0,Index=0;
	static HWND hOld;
	int SubMenuIndex=0;
	if ((hMenu==NULL) | (EmuState.WindowHandle != hOld))
		hMenu=GetMenu(EmuState.WindowHandle);
	else
		DeleteMenu(hMenu,2,MF_BYPOSITION);

	hOld=EmuState.WindowHandle;
	hSubMenu[SubMenuIndex]=CreatePopupMenu();
	memset(&Mii,0,sizeof(MENUITEMINFO));
	Mii.cbSize= sizeof(MENUITEMINFO);
	Mii.fMask = MIIM_TYPE | MIIM_SUBMENU | MIIM_ID;
	Mii.fType = MFT_STRING;
	Mii.wID = 4999;
	Mii.hSubMenu = hSubMenu[SubMenuIndex];
	Mii.dwTypeData = MenuTitle;
	Mii.cch=strlen(MenuTitle);
	InsertMenuItem(hMenu,2,TRUE,&Mii);
	SubMenuIndex++;	
	for (TempIndex=0;TempIndex<MenuIndex;TempIndex++)
	{
		if (strlen(MenuItem[TempIndex].MenuName) ==0)
			MenuItem[TempIndex].Type=STANDALONE;

		//Create Menu item in title bar if no exist already
		switch (MenuItem[TempIndex].Type)
		{
		case HEAD:
				SubMenuIndex++;
				hSubMenu[SubMenuIndex]=CreatePopupMenu();
				memset(&Mii,0,sizeof(MENUITEMINFO));
				Mii.cbSize= sizeof(MENUITEMINFO);
				Mii.fMask = MIIM_TYPE | MIIM_SUBMENU | MIIM_ID;
				Mii.fType = MFT_STRING;
				Mii.wID = MenuItem[TempIndex].MenuId;
				Mii.hSubMenu =hSubMenu[SubMenuIndex];
				Mii.dwTypeData = MenuItem[TempIndex].MenuName;
				Mii.cch=strlen(MenuItem[TempIndex].MenuName);
				InsertMenuItem(hSubMenu[0],0,FALSE,&Mii);		

			break;

		case SLAVE:
				memset(&Mii,0,sizeof(MENUITEMINFO));
				Mii.cbSize= sizeof(MENUITEMINFO);
				Mii.fMask = MIIM_TYPE |  MIIM_ID;
				Mii.fType = MFT_STRING;
				Mii.wID = MenuItem[TempIndex].MenuId;
				Mii.hSubMenu = hSubMenu[SubMenuIndex];
				Mii.dwTypeData = MenuItem[TempIndex].MenuName;
				Mii.cch=strlen(MenuItem[TempIndex].MenuName);
				InsertMenuItem(hSubMenu[SubMenuIndex],0,FALSE,&Mii);


		break;

		case STANDALONE:
			if (strlen(MenuItem[TempIndex].MenuName) ==0)
			{
				memset(&Mii,0,sizeof(MENUITEMINFO));
				Mii.cbSize= sizeof(MENUITEMINFO);
				Mii.fMask = MIIM_TYPE |  MIIM_ID;
				Mii.fType = MF_SEPARATOR; 
			//	Mii.fType = MF_MENUBARBREAK;
			//	Mii.fType = MFT_STRING;
				Mii.wID = MenuItem[TempIndex].MenuId;
				Mii.hSubMenu = hMenu;
				Mii.dwTypeData = MenuItem[TempIndex].MenuName;
				Mii.cch=strlen(MenuItem[TempIndex].MenuName);
				InsertMenuItem(hSubMenu[0],0,FALSE,&Mii);
			}
			else
			{
				memset(&Mii,0,sizeof(MENUITEMINFO));
				Mii.cbSize= sizeof(MENUITEMINFO);
				Mii.fMask = MIIM_TYPE |  MIIM_ID;
				Mii.fType = MFT_STRING;
				Mii.wID = MenuItem[TempIndex].MenuId;
				Mii.hSubMenu = hMenu;
				Mii.dwTypeData = MenuItem[TempIndex].MenuName;
				Mii.cch=strlen(MenuItem[TempIndex].MenuName);
				InsertMenuItem(hSubMenu[0],0,FALSE,&Mii);
			}
		break;
		}
	}
	DrawMenuBar(EmuState.WindowHandle);
	return;
}

/****************************************************************************/
