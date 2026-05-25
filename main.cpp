#include <string>
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <exception>
#include <iostream>

#include "irsdk/irsdk_defines.h"
#include "irsdk/irsdk_client.h"
#include "HPR.h"

irsdkCVar ir_BrakeABSactive("BrakeABSactive");    // bool[1] true if abs is currently reducing brake force pressure ()
irsdkCVar ir_Brake("Brake");    // float[1] 0=brake released to 1=max pedal force (%)

const char *irsdk_varNames()
{
	std::vector<std::string> varNames;

	// Collect all variable names
	for (int index = 0; index < irsdk_getHeader()->numVars; index++)
	{
		const irsdk_varHeader* pVar = irsdk_getVarHeaderEntry(index);
		if (pVar)
		{
			varNames.emplace_back(pVar->name);
		}
	}

	// Sort the variable names alphabetically
	std::sort(varNames.begin(), varNames.end());

	// Concatenate the names into a single space-separated string
	static std::string result; // Use a static string to persist after function returns
	result.clear();
	for (size_t i = 0; i < varNames.size(); ++i)
	{
		result += varNames[i];
		if (i < varNames.size() - 1)
		{
			result += " "; // Add a space between names
		}
	}

	return result.c_str(); // Return the C-style string
}

static std::string GetProductVersion()
{
    std::string strResult;

    char szModPath[ MAX_PATH ];
    szModPath[ 0 ] = '\0';
    GetModuleFileName( NULL, szModPath, sizeof(szModPath) );
    DWORD dwHandle;
    DWORD dwSize = GetFileVersionInfoSize( szModPath, &dwHandle );

    if( dwSize > 0 )
    {
        BYTE* pbBuf = static_cast<BYTE*>( alloca( dwSize ) );
        if( GetFileVersionInfo( szModPath, dwHandle, dwSize, pbBuf ) )
        {
            UINT uiSize;
            BYTE* lpb;
            if( VerQueryValue( pbBuf,
                               "\\VarFileInfo\\Translation",
                               (void**)&lpb,
                               &uiSize ) )
            {
                WORD* lpw = (WORD*)lpb;
                std::string strQuery;
                strQuery = "\\StringFileInfo\\";
                char buffer[10];
                sprintf_s(buffer, "%04x%04x", lpw[ 0 ], lpw[ 1 ]);
                strQuery += buffer;
                strQuery += "\\ProductVersion";
                if( VerQueryValue( pbBuf,
                                   const_cast<LPSTR>( (LPCSTR)strQuery.c_str() ),
                                   (void**)&lpb,
                                   &uiSize ) && uiSize > 0 )
                {
                    strResult = (LPCSTR)lpb;
                }
            }
        }
    }

    return strResult;
}

int main()
{
    // Single-instance guard
    HANDLE singleInstanceMutex = NULL;

    singleInstanceMutex = CreateMutexW(NULL, TRUE, L"Global\\iHaptics_SingleInstance_Mutex");
    if (singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(NULL, L"iHaptics is already running. Please first close the existing instance and try again.", L"iHaptics", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    std::string version = GetProductVersion();

    SetConsoleTitle((std::string("iHaptics v") + version).c_str());

    printf("iHaptics v%s (%s %s) https://github.com/hammer-is/iHaptics - Simagic Pedal Haptics for iRacing\n\n", version.c_str(), __DATE__, __TIME__);

    HPR hpr;
    HPR::PedalsDevice pedalsDevice = HPR::PedalsDevice::None;

    printf("Enumerating USB devices\n");
    pedalsDevice = hpr.Initialize(true, [](const std::string& info) { printf("* %s\n", info.c_str()); });

    if (pedalsDevice == HPR::PedalsDevice::None)
    {        
        std::string lastInfo;
        printf("Waiting for supported pedals to be connected...\n");

        while (pedalsDevice == HPR::PedalsDevice::None)
        {
            Sleep(5000);
            pedalsDevice = hpr.Initialize(true, [&lastInfo](const std::string& info) {
                lastInfo = info;
            });
        }
        printf("* %s\n", lastInfo.c_str());
    }

#if defined(_DEBUG)        
    printf("Testing brake vibration (1s, 20hz, 50%)\n");
    hpr.VibratePedal(HPR::Channel::Brake, 20.0f, 50.0f);
    Sleep(1000);
    hpr.VibratePedal(HPR::Channel::Brake, 0, 0.0f);

    printf("Testing clutch vibration (1s, 30hz, 50%) \n");
    hpr.VibratePedal(HPR::Channel::Clutch, 30.0f, 50.0f);
    Sleep(1000);
    hpr.VibratePedal(HPR::Channel::Clutch, 0, 0.0f);

    printf("Testing throttle vibration (1s, 40hz, 50%) \n");
    hpr.VibratePedal(HPR::Channel::Throttle, 40.0f, 50.0f);
    Sleep(1000);
    hpr.VibratePedal(HPR::Channel::Throttle, 0, 0.0f);
#endif

    printf("Waiting for iRacing connection...\n");

    bool connected = false;

    while( true )
    {
        irsdkClient& irsdk = irsdkClient::instance();
        irsdk.waitForData(16);

        if( !irsdk.isConnected() )
        {
            if(connected)
            {
                printf("iRacing disconnected\n");
                connected = false;
            }
                continue;
        }

        if(!connected)
        {
            printf("iRacing connected\n");
            connected = true;

#if defined(_DEBUG)        
            printf("Available variables: %s\n",irsdk_varNames());
#endif
        }
        
        const float currentBrake = ir_Brake.getFloat();
        const bool absActive = ir_BrakeABSactive.getBool();        

        int m_hpr_abs_frequency = 20;
        float m_hpr_abs_amplitude_min = 30.0f;
        float m_hpr_abs_amplitude_max = 60.0f;
        
        if (absActive && currentBrake > 0.02f)
        {
#if defined(_DEBUG)        
            printf("Brake: %.2f, ABS Active: %s\n", currentBrake, absActive ? "Yes" : "No");
#endif            
            float amplitude = (currentBrake * (m_hpr_abs_amplitude_max - m_hpr_abs_amplitude_min) + m_hpr_abs_amplitude_min);
            hpr.VibratePedal(HPR::Channel::Brake, m_hpr_abs_frequency, amplitude);
        }
        else
        {
            hpr.VibratePedal(HPR::Channel::Brake, 0, 0.0f);
        }
                
    }
}
