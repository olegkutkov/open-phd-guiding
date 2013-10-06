/*
 *  ascom.cpp
 *  PHD Guiding
 *
 *  Created by Craig Stark.
 *  Copyright (c) 2006-2010 Craig Stark.
 *  All rights reserved.
 *
 *  Modified by Bret McKee
 *  Copyright (c) 2012-2013 Bret McKee
 *  All rights reserved.
 *
 *  This source code is distributed under the following "BSD" license
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *    Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *    Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *    Neither the name of Craig Stark, Stark Labs nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "phd.h"

#ifdef GUIDE_ASCOM

#include "comdispatch.h"

#include <wx/msw/ole/oleutils.h>
#include <objbase.h>
#include <ole2ver.h>
#include <initguid.h>
#include <wx/textfile.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>

struct AutoASCOMDriver
{
    IDispatch *m_driver;
    AutoASCOMDriver(IGlobalInterfaceTable *igit, DWORD cookie)
    {
        if (FAILED(igit->GetInterfaceFromGlobal(cookie, IID_IDispatch, (LPVOID *) &m_driver)))
        {
            throw ERROR_INFO("ASCOM Scope: Cannot get interface from Global Interface Table");
        }
    }
    ~AutoASCOMDriver()
    {
        m_driver->Release();
    }
    operator IDispatch *() const { return m_driver; }
    IDispatch *operator->() const { return m_driver; }
    IDispatch *get() const { return m_driver; }
};

ScopeASCOM::ScopeASCOM(const wxString& choice)
{
    m_pIGlobalInterfaceTable = NULL;
    m_dwCookie = 0;
    m_choice = choice;
}

ScopeASCOM::~ScopeASCOM(void)
{
    if (m_pIGlobalInterfaceTable)
    {
        if (m_dwCookie)
        {
            m_pIGlobalInterfaceTable -> RevokeInterfaceFromGlobal(m_dwCookie);
            m_dwCookie = 0;
        }
        m_pIGlobalInterfaceTable -> Release();
        m_pIGlobalInterfaceTable = NULL;
    }
}

static wxString displayName(const wxString& ascomName)
{
    if (ascomName.Find(_T("ASCOM")) != wxNOT_FOUND)
        return ascomName;
    return ascomName + _T(" (ASCOM)");
}

// map descriptive name to progid
static std::map<wxString, wxString> s_progid;

wxArrayString ScopeASCOM::EnumAscomScopes()
{
    wxArrayString list;
    list.Add(_T("ASCOM Telescope Chooser"));

    try
    {
        DispatchObj profile;
        if (!profile.Create(L"ASCOM.Utilities.Profile"))
            throw ERROR_INFO("ASCOM Camera: could not instantiate ASCOM profile class");

        VARIANT res;
        if (!profile.InvokeMethod(&res, L"RegisteredDevices", L"Telescope"))
            throw ERROR_INFO("ASCOM Camera: could not query registered telescope devices");

        DispatchClass ilist_class;
        DispatchObj ilist(res.pdispVal, &ilist_class);

        VARIANT vcnt;
        if (!ilist.GetProp(&vcnt, L"Count"))
            throw ERROR_INFO("ASCOM Camera: could not query registered telescopes");

        unsigned int const count = vcnt.intVal;
        DispatchClass kvpair_class;

        for (unsigned int i = 0; i < count; i++)
        {
            VARIANT kvpres;
            if (ilist.GetProp(&kvpres, L"Item", i))
            {
                DispatchObj kvpair(kvpres.pdispVal, &kvpair_class);
                VARIANT vkey, vval;
                if (kvpair.GetProp(&vkey, L"Key") && kvpair.GetProp(&vval, L"Value"))
                {
                    wxString ascomName = wxBasicString(vval.bstrVal).Get();
                    wxString displName = displayName(ascomName);
                    wxString progid = wxBasicString(vkey.bstrVal).Get();
                    s_progid[displName] = progid;
                    list.Add(displName);
                }
            }
        }
    }
    catch (const wxString& msg)
    {
        POSSIBLY_UNUSED(msg);
    }

    return list;
}

static bool ChooseASCOMScope(BSTR *res)
{
    DispatchObj chooser;
    if (!chooser.Create(L"DriverHelper.Chooser"))
    {
        wxMessageBox(_("Failed to find the ASCOM Chooser. Make sure it is installed"), _("Error"), wxOK | wxICON_ERROR);
        return false;
    }

    if (!chooser.PutProp(L"DeviceType", L"Telescope"))
    {
        wxMessageBox(_("Failed to set the Chooser's type to Telescope. Something is wrong with ASCOM"), _("Error"), wxOK | wxICON_ERROR);
        return false;
    }

    // Look in Registry to see if there is a default
    wxString wx_ProgID = pConfig->Global.GetString("/scope/ascom/ScopeID", _T(""));
    BSTR bstr_ProgID = wxBasicString(wx_ProgID).Get();

    VARIANT vchoice;
    if (!chooser.InvokeMethod(&vchoice, L"Choose", bstr_ProgID))
    {
        wxMessageBox(_("Failed to run the Telescope Chooser. Something is wrong with ASCOM"), _("Error"), wxOK | wxICON_ERROR);
        return false;
    }

    if (SysStringLen(vchoice.bstrVal) == 0)
        return false; // use hit cancel

    // Save name of scope
    pConfig->Global.SetString("/scope/ascom/ScopeID", vchoice.bstrVal);

    *res = vchoice.bstrVal;
    return true;
}

static bool GetDriverProgId(BSTR *progid, const wxString& choice)
{
    if (choice.Find(_T("Chooser")) != wxNOT_FOUND)
    {
        if (!ChooseASCOMScope(progid))
            return false;
    }
    else
    {
        wxString progidstr = s_progid[choice];
        *progid = wxBasicString(progidstr).Get();
    }
    return true;
}

bool ScopeASCOM::Connect(void)
{
    bool bError = false;

    try
    {
        Debug.AddLine(wxString::Format("Connecting"));

        if (IsConnected())
        {
            wxMessageBox("Scope already connected",_("Error"));
            throw ERROR_INFO("ASCOM Scope: Connected - Already Connected");
        }

        BSTR bstr_progid;
        if (!GetDriverProgId(&bstr_progid, m_choice))
        {
            throw ERROR_INFO("ASCOM Scope: Chooser returned an error");
        }

        DispatchObj pScopeDriver;

        if (!pScopeDriver.Create(bstr_progid))
        {
            wxMessageBox(_T("Could not establish instance of ") + wxString(bstr_progid), _("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not establish ASCOM Scope instance");
        }
        Debug.AddLine(wxString::Format("pScopeDriver = 0x%p", pScopeDriver.IDisp()));

        // --- get the dispatch IDs we need ...

        // ... get the dispatch ID for the Connected property ...
        if (!pScopeDriver.GetDispatchId(&dispid_connected, L"Connected"))
        {
            wxMessageBox(_T("ASCOM driver problem -- cannot connect"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not get the dispatch id for the Connected property");
        }

        // ... get thie dispatch ID for the Name property ...
        if (!pScopeDriver.GetDispatchId(&dispid_name, L"Name"))
        {
            wxMessageBox(_T("Can't get the name of the scope -- ASCOM driver missing the name"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not get the dispatch id for the Name property");
        }

        // ... get the dispatch ID for the "CanPulseGuide" property ....
        if (!pScopeDriver.GetDispatchId(&dispid_canpulseguide, L"CanPulseGuide"))
        {
            wxMessageBox(_T("ASCOM driver missing the CanPulseGuide property"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not get the dispatch id for the CanPulseGuide property");
        }

        // ... get the dispatch ID for the "IsPulseGuiding" property ....
        m_bCanCheckPulseGuiding = true;
        if (!pScopeDriver.GetDispatchId(&dispid_ispulseguiding, L"IsPulseGuiding"))
        {
            m_bCanCheckPulseGuiding = false;
            Debug.AddLine(wxString::Format("cannot get dispid_ispulseguiding = %d", dispid_ispulseguiding));
            // don't fail if we can't get the status on this - can live without it as it's really a safety net for us
        }

        // ... get the dispatch ID for the "Slewing" property ....
        if (!pScopeDriver.GetDispatchId(&dispid_isslewing, L"Slewing"))
        {
            wxMessageBox(_T("ASCOM driver missing the Slewing property"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not get the dispatch id for the Slewing property");
        }

        // ... get the dispatch ID for the "PulseGuide" property ....
        if (!pScopeDriver.GetDispatchId(&dispid_pulseguide, L"PulseGuide"))
        {
            wxMessageBox(_T("ASCOM driver missing the PulseGuide property"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not get the dispatch id for the PulseGuide property");
        }

        // ... get the dispatch ID for the "Declination" property ....
        m_bCanGetCoordinates = true;
        if (!pScopeDriver.GetDispatchId(&dispid_declination, L"Declination"))
        {
            m_bCanGetCoordinates = false;
            Debug.AddLine(wxString::Format("cannot get dispid_declination = %d", dispid_declination));
        }
        else if (!pScopeDriver.GetDispatchId(&dispid_rightascension, L"RightAscension"))
        {
            Debug.AddLine(wxString::Format("cannot get dispid_rightascension = %d", dispid_rightascension));
            m_bCanGetCoordinates = false;
        }
        else if (!pScopeDriver.GetDispatchId(&dispid_siderealtime, L"SiderealTime"))
        {
            Debug.AddLine(wxString::Format("cannot get dispid_siderealtime = %d", dispid_siderealtime));
            m_bCanGetCoordinates = false;
        }

        m_bCanSlew = true;
        if (!pScopeDriver.GetDispatchId(&dispid_canslew, L"CanSlew"))
        {
            m_bCanSlew = false;
            Debug.AddLine(wxString::Format("cannot get dispid_canslew = %d", dispid_canslew));
        }
        else if (!pScopeDriver.GetDispatchId(&dispid_slewtocoordinates, L"SlewToCoordinates"))
        {
            m_bCanSlew = false;
            Debug.AddLine(wxString::Format("cannot get dispid_slewtocoordinates = %d", dispid_slewtocoordinates));
        }

        // ... get the dispatch IDs for the two guide rate properties - if we can't get them, no sweat, using only in step calculator
        m_bCanGetGuideRates = true;         // Likely case, required for any ASCOM driver at V2 or later
        if (!pScopeDriver.GetDispatchId(&dispid_decguiderate, L"GuideRateDeclination"))
        {
            Debug.AddLine(wxString::Format("cannot get dispid_decguiderate = %d", dispid_decguiderate));
            m_bCanGetGuideRates = false;
            // don't throw if we can't get this one
        }
        else if (!pScopeDriver.GetDispatchId(&dispid_raguiderate, L"GuideRateRightAscension"))
        {
            Debug.AddLine(wxString::Format("cannot get dispid_raguiderate = %d", dispid_raguiderate));
            m_bCanGetGuideRates = false;
            // don't throw if we can't get this one
        }

        // we have all the IDs we need - time to start using them

        // ... set the Connected property to true....
        if (!pScopeDriver.PutProp(dispid_connected, true))
        {
            wxMessageBox(_T("ASCOM driver problem during connection: ") + wxString(pScopeDriver.Excep().bstrDescription),
                _("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not set Connected property to true");
        }

        // get the scope name
        VARIANT vRes;
        if (!pScopeDriver.GetProp(&vRes, dispid_name))
        {
            wxMessageBox(_T("ASCOM driver problem getting Name property"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not get the scope name");
        }

        char *cp = uni_to_ansi(vRes.bstrVal); // Get ProgID in ANSI
        m_Name = cp;
        free(cp);

        Debug.AddLine(wxString::Format("Scope reports its name as ") + m_Name);

        // see if we can pulse guide
        if (!pScopeDriver.GetProp(&vRes, dispid_canpulseguide) || !vRes.boolVal)
        {
            wxMessageBox(_T("ASCOM driver does not support the needed Pulse Guide method."),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Cannot pulseguide");
        }

        // see if we can slew
        if (m_bCanSlew)
        {
            if (!pScopeDriver.GetProp(&vRes, dispid_canslew))
            {
                Debug.AddLine("ASCOM scope got error invoking CanSlew");
                m_bCanSlew = false;
            }
            else if (!vRes.boolVal)
            {
                Debug.AddLine("ASCOM scope reports CanSlew = false");
                m_bCanSlew = false;
            }
        }

        // store the driver interface in the global table for access by other threads
        if (m_pIGlobalInterfaceTable == NULL)
        {
            // first find the global table
            if (FAILED(::CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, IID_IGlobalInterfaceTable,
                    (void **)&m_pIGlobalInterfaceTable)))
            {
                throw ERROR_INFO("ASCOM Scope: Cannot CoCreateInstance of Global Interface Table");
            }
        }

        assert(m_pIGlobalInterfaceTable);

        // add the Interface to the global table. Any errors past this point need to remove the interface from the global table.
        if (FAILED(m_pIGlobalInterfaceTable->RegisterInterfaceInGlobal(pScopeDriver.IDisp(), IID_IDispatch, &m_dwCookie)))
        {
            throw ERROR_INFO("ASCOM Scope: Cannot register with Global Interface Table");
        }

        assert(m_dwCookie);

        pFrame->SetStatusText(Name()+_(" connected"));
        Scope::Connect();

        Debug.AddLine(wxString::Format("Connect success"));
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bError = true;
    }

    return bError;
}

bool ScopeASCOM::Disconnect(void)
{
    bool bError = false;

    try
    {
        DISPPARAMS dispParms;
        DISPID didPut = DISPID_PROPERTYPUT;
        VARIANTARG rgvarg[1];
        HRESULT hr;
        EXCEPINFO excep;
        VARIANT vRes;

        Debug.AddLine(wxString::Format("Disconnecting"));

        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: attempt to disconnect when not connected");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);

        // ... set the Connected property to false....
        rgvarg[0].vt = VT_BOOL;
        rgvarg[0].boolVal = FALSE;
        dispParms.cArgs = 1;
        dispParms.rgvarg = rgvarg;
        dispParms.cNamedArgs = 1;                   // PropPut kludge
        dispParms.rgdispidNamedArgs = &didPut;
        if(FAILED(hr = pScopeDriver->Invoke(dispid_connected,IID_NULL,LOCALE_USER_DEFAULT,DISPATCH_PROPERTYPUT,&dispParms,&vRes,&excep, NULL))) {
            wxMessageBox(_T("ASCOM driver problem during connection"),_("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("ASCOM Scope: Could not set Connected property to false");
        }

        // cleanup the global interface table
        m_pIGlobalInterfaceTable->RevokeInterfaceFromGlobal(m_dwCookie);
        m_dwCookie = 0;
        m_pIGlobalInterfaceTable->Release();
        m_pIGlobalInterfaceTable = NULL;

        Debug.AddLine(wxString::Format("Disconnected Successfully"));
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bError = true;
    }

    Scope::Disconnect();

    return bError;
}

bool ScopeASCOM::Guide(const GUIDE_DIRECTION direction, const int duration)
{
    bool bError = false;

    try
    {
        DISPPARAMS dispParms;
        VARIANTARG rgvarg[2];
        HRESULT hr;
        EXCEPINFO excep;
        VARIANT vRes;
        int i;
        wxStopWatch swatch;

        Debug.AddLine(wxString::Format("Guiding  Dir = %d, Dur = %d", direction, duration));

        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: attempt to guide when not connected");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        // First, check to see if already moving
        if (IsGuiding(&scope))
        {
            Debug.AddLine("Entered PulseGuideScope while moving");
            for(i=0;(i<20) && IsGuiding(&scope);i++) {
                Debug.AddLine("Still moving");
                wxMilliSleep(50);
            }
            if (i==20) {
                Debug.AddLine("Moving after 1s still - aborting");
                throw ERROR_INFO("ASCOM Scope: scope is still moving after 1 second");
            }
            else {
                Debug.AddLine("Movement stopped - continuing");
            }
        }

        // Do the move
        // Convert into the right direction #'s if buttons used

        rgvarg[1].vt = VT_I2;
        rgvarg[1].iVal =  direction;
        rgvarg[0].vt = VT_I4;
        rgvarg[0].lVal = (long) duration;
        dispParms.cArgs = 2;
        dispParms.rgvarg = rgvarg;
        dispParms.cNamedArgs = 0;
        dispParms.rgdispidNamedArgs =NULL;
        swatch.Start();

        if(FAILED(hr = pScopeDriver->Invoke(dispid_pulseguide,IID_NULL,LOCALE_USER_DEFAULT,DISPATCH_METHOD,
                                        &dispParms,&vRes,&excep,NULL)))
        {
            Debug.AddLine(wxString::Format("pulseguide fails, pScopeDriver = 0x%p", pScopeDriver.get()));
            throw ERROR_INFO("ASCOM Scope: pulseguide command failed");
        }
        if (swatch.Time() < (long) duration) {
            Debug.AddLine("PulseGuide returned control before completion");
        }

        while (IsGuiding(&scope)) {
            SleepEx(50,true);
            Debug.AddLine("waiting 50ms");
        }
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bError = true;
    }

    return bError;
}

bool ScopeASCOM::IsGuiding(DispatchObj *scope)
{
    bool bReturn = true;

    try
    {
        Debug.AddLine(wxString::Format("IsGuiding() entered for pScopeDriver=%p", scope->IDisp()));

        if (!m_bCanCheckPulseGuiding)
        {
            // Assume all is good - best we can do as this is really a fail-safe check.  If we can't call this property (lame driver) guides will have to
            // enforce the wait.  But, enough don't support this that we can't throw an error.
            throw ERROR_INFO("ASCOM Scope: IsGuiding - !m_bCanCheckPulseGuiding");
        }

        // First, check to see if already moving
        VARIANT vRes;
        if (!scope->GetProp(&vRes, dispid_ispulseguiding))
        {
            wxMessageBox(_T("ASCOM driver failed checking IsPulseGuiding"),_("Error"), wxOK | wxICON_ERROR);
            Debug.AddLine(wxString::Format("IsPulseGuding fails, pScopeDriver = 0x%p", scope->IDisp()));
            throw ERROR_INFO("ASCOM Scope: IsGuiding - IsPulseGuiding failed");
        }

        // if we were not guiding, see if we are slewing
        if (!vRes.boolVal)
        {
            bReturn = IsSlewing(scope);
        }
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bReturn = false;
    }

    Debug.AddLine(wxString::Format("IsGuiding returns %d", bReturn));

    return bReturn;
}

bool ScopeASCOM::IsSlewing(DispatchObj *scope)
{
    VARIANT vRes;
    if (!scope->GetProp(&vRes, dispid_isslewing))
    {
        wxMessageBox(_T("ASCOM driver failed checking Slewing"), _("Error"), wxOK | wxICON_ERROR);
        return false;
    }

    return vRes.boolVal == VARIANT_TRUE;
}

bool ScopeASCOM::Slewing(void)
{
    bool bReturn = true;

    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: Cannot check Slewing when not connected to mount");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        bReturn = IsSlewing(&scope);
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bReturn = false;
    }

    return bReturn;
}

bool ScopeASCOM::IsGuiding()
{
    bool bReturn = true;

    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: Cannot check IsGuiding when not connected to mount");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        bReturn = IsGuiding(&scope);
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bReturn = false;
    }

    return bReturn;
}

bool ScopeASCOM::HasNonGuiMove(void)
{
    return true;
}

// Warning: declination is returned in units of radians, not the decimal degrees used in the ASCOM interface
double ScopeASCOM::GetDeclination(void)
{
    double dReturn = Scope::GetDeclination();

    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: cannot get Declination when not connected to mount");
        }

        if (!m_bCanGetCoordinates)
        {
            throw THROW_INFO("!m_bCanGetCoordinates");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        VARIANT vRes;
        if (!scope.GetProp(&vRes, dispid_declination))
        {
            throw ERROR_INFO("GetDeclination() fails");
        }

        dReturn = vRes.dblVal / 180.0 * M_PI;
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        m_bCanGetCoordinates = false;
    }

    Debug.AddLine("ScopeASCOM::GetDeclination() returns %.4f", dReturn);

    return dReturn;
}

// Return RA and Dec guide rates in native ASCOM units, degrees/sec.
// Convention is to return true on an error
bool ScopeASCOM::GetGuideRate(double *pRAGuideRate, double *pDecGuideRate)
{
    bool bError = false;

    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: cannot get guide rates when not connected");
        }

        if (!m_bCanGetGuideRates)
        {
            throw THROW_INFO("ASCOM Scope: not capable of getting guide rates");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        VARIANT vRes;

        if (!scope.GetProp(&vRes, dispid_decguiderate))
        {
            throw ERROR_INFO("ASCOM Scope: GuideRateDec() failed");
        }

        *pDecGuideRate = vRes.dblVal;

        if (!scope.GetProp(&vRes, dispid_raguiderate))
        {
            throw ERROR_INFO("ASCOM Scope: GuideRateRA() failed");
        }

        *pRAGuideRate = vRes.dblVal;
    }
    catch (wxString Msg)
    {
        bError = true;
        POSSIBLY_UNUSED(Msg);
    }

    Debug.AddLine("ScopeASCOM::GetGuideRates() returns %u %.4f %.4f", bError,
        bError ? 0.0 : *pDecGuideRate, bError ? 0.0 : *pRAGuideRate);

    return bError;
}

bool ScopeASCOM::GetCoordinates(double *ra, double *dec, double *siderealTime)
{
    bool bError = false;

    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: cannot get coordinates when not connected");
        }

        if (!m_bCanGetCoordinates)
        {
            throw THROW_INFO("ASCOM Scope: not capable of getting coordinates");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        VARIANT vRA;

        if (!scope.GetProp(&vRA, dispid_rightascension))
        {
            throw ERROR_INFO("ASCOM Scope: get right ascension failed");
        }

        VARIANT vDec;

        if (!scope.GetProp(&vDec, dispid_declination))
        {
            throw ERROR_INFO("ASCOM Scope: get declination failed");
        }

        VARIANT vST;

        if (!scope.GetProp(&vST, dispid_siderealtime))
        {
            throw ERROR_INFO("ASCOM Scope: get sidereal time failed");
        }

        *ra = vRA.dblVal;
        *dec = vDec.dblVal;
        *siderealTime = vST.dblVal;
    }
    catch (wxString Msg)
    {
        bError = true;
        POSSIBLY_UNUSED(Msg);
    }

    return bError;
}

bool ScopeASCOM::CanSlew(void)
{
    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: cannot get CanSlew property when not connected to mount");
        }

        return m_bCanSlew;
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        return false;
    }
}

bool ScopeASCOM::SlewToCoordinates(double ra, double dec)
{
    bool bError = false;

    try
    {
        if (!IsConnected())
        {
            throw ERROR_INFO("ASCOM Scope: cannot slew when not connected");
        }

        if (!m_bCanSlew)
        {
            throw THROW_INFO("ASCOM Scope: not capable of slewing");
        }

        AutoASCOMDriver pScopeDriver(m_pIGlobalInterfaceTable, m_dwCookie);
        DispatchObj scope(pScopeDriver, NULL);

        VARIANT vRes;

        if (!scope.InvokeMethod(&vRes, dispid_slewtocoordinates, ra, dec))
        {
            throw ERROR_INFO("ASCOM Scope: slew to coordinates failed");
        }
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bError = true;
    }

    return bError;
}

#endif /* GUIDE_ASCOM */
