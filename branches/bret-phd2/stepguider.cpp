/*
 *  stepguider.cpp
 *  PHD Guiding
 *
 *  Created by Bret McKee
 *  Copyright (c) 2013 Bret McKee
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
 *    Neither the name of Bret McKee, Dad Dog Development, nor the names of its
 *     Craig Stark, Stark Labs nor the names of its
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

#include "image_math.h"
#include "wx/textfile.h"
#include "socket_server.h"

static const int DefaultCalibrationAmount = 5;
static const double DEC_BACKLASH_DISTANCE = 0.0;

StepGuider::StepGuider(void) :
    Mount(DEC_BACKLASH_DISTANCE)
{
    int calibrationAmount = PhdConfig.GetInt("/stepguider/CalibrationAmount", DefaultCalibrationAmount);
    SetCalibrationAmount(calibrationAmount);
}

StepGuider::~StepGuider(void)
{
}

int StepGuider::GetCalibrationAmount(void)
{
    return m_calibrationAmount;
}

bool StepGuider::BacklashClearingFailed(void)
{
    bool bError = false;

    wxMessageBox(_T("Unable to clear StepGuider DEC backlash -- should not happen. Calibration failed."), _("Error"), wxOK | wxICON_ERROR);

    return true;
}

bool StepGuider::SetCalibrationAmount(int calibrationAmount)
{
    bool bError = false;

    try
    {
        if (calibrationAmount <= 0.0)
        {
            throw ERROR_INFO("invalid calibrationAmount");
        }

        m_calibrationAmount = calibrationAmount;

    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        bError = true;
        m_calibrationAmount = DefaultCalibrationAmount;
    }

    PhdConfig.SetInt("/stepguider/CalibrationAmount", m_calibrationAmount);

    return bError;
}

void MyFrame::OnConnectStepGuider(wxCommandEvent& WXUNUSED(event))
{
    StepGuider *pStepGuider = NULL;

    try
    {
        if (pGuider->GetState() > STATE_SELECTED)
        {
            throw ERROR_INFO("Connecting Step Guider when state > STATE_SELECTED");
        }

        if (CaptureActive)
        {
            throw ERROR_INFO("Connecting Step Guider when CaptureActive");
        }

        if (pSecondaryMount)
        {
            /*
             * If there is a secondary mount, then the primary mount (aka pMount)
             * is a StepGuider.  Get rid of the current primary mount,
             * and move the secondary mount back to being the primary mount
             */
            assert(pMount);

            if (pMount->IsConnected())
            {
                pMount->Disconnect();
            }

            delete pMount;
            pMount = pSecondaryMount;
            pSecondaryMount = NULL;
            SetStatusText(_T(""),4);
        }

        assert(pMount);

#if 0
        if (!pMount->IsConnected())
        {
            wxMessageBox(_T("Please connect a scope before connecting an AO"), _("Error"), wxOK | wxICON_ERROR);
            throw ERROR_INFO("attempt to connect AO with no scope connected");
        }
#endif

        if (mount_menu->IsChecked(AO_NONE))
        {
            // nothing to do here
        }
#ifdef STEPGUIDER_SXAO
        else if (mount_menu->IsChecked(AO_SXAO))
        {
            pStepGuider = new StepGuiderSxAO();
        }
#endif

        if (pStepGuider)
        {
            if (pStepGuider->Connect())
            {
                SetStatusText("AO connect failed", 1);
                throw ERROR_INFO("unable to connect to AO");
            }

            SetStatusText(_("Adaptive Optics Connected"), 1);
            SetStatusText(_T("AO"),4);

            // successful connection - switch the step guider in

            assert(pSecondaryMount == NULL);
            pSecondaryMount = pMount;
            pMount = pStepGuider;
            pStepGuider = NULL;

            // at this point, the AO is connected and active. Even if we
            // fail from here on out that doesn't change

            // now store the stepguider we selected so we can use it as the default next time.
            wxMenuItemList items = mount_menu->GetMenuItems();
            wxMenuItemList::iterator iter;

            for(iter = items.begin(); iter != items.end(); iter++)
            {
                wxMenuItem *pItem = *iter;

                if (pItem->IsChecked())
                {
                    wxString value = pItem->GetItemLabelText();
                    PhdConfig.SetString("/stepguider/LastMenuChoice", value);
                    SetStatusText(value + " connected");
                    break;
                }
            }
        }
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);

        mount_menu->FindItem(AO_NONE)->Check(true);
        delete pStepGuider;
        pStepGuider = NULL;
    }

    assert(pMount && pMount->IsConnected());
    assert(!pSecondaryMount || pSecondaryMount->IsConnected());

    UpdateButtonsStatus();
}

bool StepGuider::CalibrationMove(GUIDE_DIRECTION direction)
{
    Debug.AddLine(wxString::Format("stepguider CalibrationMove(%d)", direction));
    return Step(direction, m_calibrationAmount);
}

double StepGuider::Move(GUIDE_DIRECTION direction, double amount, bool normalMove)
{
    int steps = 0;

    try
    {
        // Compute the required guide steps
        if (m_guidingEnabled)
        {
            char directionName = '?';
            double steps = 0.0;

            switch (direction)
            {
                case NORTH:
                case SOUTH:
                    directionName = (direction==SOUTH)?'S':'N';
                    break;
                case EAST:
                case WEST:
                    directionName = (direction==EAST)?'E':'W';
                    break;
            }

            // Acutally do the guide
            steps = (int)(amount + 0.5);
            assert(steps >= 0);

            if (steps > 0)
            {
                if (Step(direction, steps))
                {
                    throw ERROR_INFO("step failed");
                }
            }

            if (CurrentPosition(direction) > 0.75 * MaxStepsFromCenter(direction) &&
                pSecondaryMount &&
                !pSecondaryMount->IsBusy())
            {
                // we have to transform our notion of where we are (which is in "AO Coordinates")
                // into "Camera Coordinates" so we can move the other mount to make the move

                double raDistance = CurrentPosition(NORTH)*DecRate();
                double decDistance = CurrentPosition(EAST)*RaRate();
                Point cameraOffset;

                if (TransformMoutCoordinatesToCameraCoordinates(raDistance, decDistance, cameraOffset))
                {
                    throw ERROR_INFO("MountToCamera failed");
                }

                pFrame->ScheduleMoveSecondary(pSecondaryMount, cameraOffset, false);
            }
        }
    }
    catch (wxString Msg)
    {
        POSSIBLY_UNUSED(Msg);
        steps = -1;
    }

    return (double)steps;
}

bool StepGuider::IsAtCalibrationLimit(GUIDE_DIRECTION direction)
{
    bool bReturn = (CurrentPosition(direction) >= 0.79 * MaxStepsFromCenter(direction));
    Debug.AddLine(wxString::Format("isatlimit=%d current=%d, max=%d", bReturn, CurrentPosition(direction), MaxStepsFromCenter(direction)));

    return bReturn;
}

double StepGuider::CalibrationTime(int nCalibrationSteps)
{
    return nCalibrationSteps * m_calibrationAmount;
}

ConfigDialogPane *StepGuider::GetConfigDialogPane(wxWindow *pParent)
{
    return new StepGuiderConfigDialogPane(pParent, this);
}

StepGuider::StepGuiderConfigDialogPane::StepGuiderConfigDialogPane(wxWindow *pParent, StepGuider *pStepGuider)
    : MountConfigDialogPane(pParent, pStepGuider)
{
    int width;

    m_pStepGuider = pStepGuider;

    width = StringWidth(_T("00000"));
    m_pCalibrationAmount = new wxSpinCtrl(pParent, wxID_ANY,_T("foo2"), wxPoint(-1,-1),
            wxSize(width+30, -1), wxSP_ARROW_KEYS, 0, 10000, 1000,_("Cal_Dur"));

    DoAdd(_("Calibration Amount"), m_pCalibrationAmount,
        wxString::Format(_T("How many steps should be issued per calibration cycle. Default = %d, increase for short f/l scopes and decrease for longer f/l scopes"), DefaultCalibrationAmount));

}

StepGuider::StepGuiderConfigDialogPane::~StepGuiderConfigDialogPane(void)
{
}

void StepGuider::StepGuiderConfigDialogPane::LoadValues(void)
{
    MountConfigDialogPane::LoadValues();
    m_pCalibrationAmount->SetValue(m_pStepGuider->GetCalibrationAmount());
}

void StepGuider::StepGuiderConfigDialogPane::UnloadValues(void)
{
    m_pStepGuider->SetCalibrationAmount(m_pCalibrationAmount->GetValue());
    MountConfigDialogPane::UnloadValues();
}
