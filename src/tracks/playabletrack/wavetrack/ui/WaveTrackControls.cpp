/**********************************************************************

Audacity: A Digital Audio Editor

WaveTrackControls.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../../../Audacity.h"
#include "WaveTrackControls.h"

#include "../../../../Experimental.h"

#include "../../ui/PlayableTrackButtonHandles.h"
#include "WaveTrackSliderHandles.h"

#include "WaveTrackView.h"
#include "../../../../AudioIOBase.h"
#include "../../../../CellularPanel.h"
#include "../../../../Menus.h"
#include "../../../../Project.h"
#include "../../../../ProjectAudioIO.h"
#include "../../../../ProjectHistory.h"
#include "../../../../RefreshCode.h"
#include "../../../../ShuttleGui.h"
#include "../../../../TrackPanelAx.h"
#include "../../../../TrackPanelMouseEvent.h"
#include "../../../../WaveTrack.h"
#include "../../../../widgets/PopupMenuTable.h"
#include "../../../../effects/RealtimeEffectManager.h"
#include "../../../../ondemand/ODManager.h"
#include "../../../../prefs/PrefsDialog.h"
#include "../../../../prefs/SpectrumPrefs.h"
#include "../../../../prefs/ThemePrefs.h"
#include "../../../../prefs/WaveformPrefs.h"
#include "../../../../widgets/AudacityMessageBox.h"

#include <wx/combobox.h>
#include <wx/frame.h>
#include <wx/sizer.h>

namespace
{
   /// Puts a check mark at a given position in a menu.
   template<typename Pred>
   void SetMenuChecks(wxMenu & menu, const Pred &pred)
   {
      for (auto &item : menu.GetMenuItems())
      {
         if (item->IsCheckable()) {
            auto id = item->GetId();
            menu.Check(id, pred(id));
         }
      }
   }
}

WaveTrackControls::~WaveTrackControls()
{
}


std::vector<UIHandlePtr> WaveTrackControls::HitTest
(const TrackPanelMouseState & st,
 const AudacityProject *pProject)
{
   // Hits are mutually exclusive, results single
   const wxMouseState &state = st.state;
   const wxRect &rect = st.rect;
   if (state.ButtonIsDown(wxMOUSE_BTN_LEFT)) {
      auto track = FindTrack();
      std::vector<UIHandlePtr> results;
      auto result = [&]{
         UIHandlePtr result;
         if (NULL != (result = MuteButtonHandle::HitTest(
            mMuteHandle, state, rect, pProject, track)))
            return result;

         if (NULL != (result = SoloButtonHandle::HitTest(
            mSoloHandle, state, rect, pProject, track)))
            return result;

         if (NULL != (result = GainSliderHandle::HitTest(
            mGainHandle, state, rect, track)))
            return result;

         if (NULL != (result = PanSliderHandle::HitTest(
            mPanHandle, state, rect, track)))
            return result;

         return result;
      }();
      if (result) {
         results.push_back(result);
         return results;
      }
   }

   return PlayableTrackControls::HitTest(st, pProject);
}

enum {
   OnRate8ID = 30000,      // <---
   OnRate11ID,             //    |
   OnRate16ID,             //    |
   OnRate22ID,             //    |
   OnRate44ID,             //    |
   OnRate48ID,             //    | Leave these in order
   OnRate88ID,             //    |
   OnRate96ID,             //    |
   OnRate176ID,            //    |
   OnRate192ID,            //    |
   OnRate352ID,            //    |
   OnRate384ID,            //    |
   OnRateOtherID,          //    |
   //    |
   On16BitID,              //    |
   On24BitID,              //    |
   OnFloatID,              // <---

   OnWaveformID,
   OnWaveformDBID,
   OnSpectrumID,
   OnSpectrogramSettingsID,

   OnChannelLeftID,
   OnChannelRightID,
   OnChannelMonoID,

   OnMergeStereoID,
   OnWaveColorID,
   OnInstrument1ID,
   OnInstrument2ID,
   OnInstrument3ID,
   OnInstrument4ID,

   OnSwapChannelsID,
   OnSplitStereoID,
   OnSplitStereoMonoID,

   ChannelMenuID,
};


//=============================================================================
// Table class for a sub-menu
class WaveColorMenuTable : public PopupMenuTable
{
   WaveColorMenuTable() : mpData(NULL) {}
   DECLARE_POPUP_MENU(WaveColorMenuTable);

public:
   static WaveColorMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = NULL;
   }

   PlayableTrackControls::InitMenuData *mpData;

   int IdOfWaveColor(int WaveColor);
   void OnWaveColorChange(wxCommandEvent & event);
};

WaveColorMenuTable &WaveColorMenuTable::Instance()
{
   static WaveColorMenuTable instance;
   return instance;
}

void WaveColorMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<PlayableTrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   auto WaveColorId = IdOfWaveColor( pTrack->GetWaveColorIndex());
   SetMenuChecks(*pMenu, [=](int id){ return id == WaveColorId; });

   AudacityProject *const project = ::GetActiveProject();
   bool unsafe = ProjectAudioIO::Get( *project ).IsAudioActive();
   for (int i = OnInstrument1ID; i <= OnInstrument4ID; i++) {
      pMenu->Enable(i, !unsafe);
   }
}

const TranslatableString GetWaveColorStr(int colorIndex)
{
   return XO("Instrument %i").Format( colorIndex+1 );
}


BEGIN_POPUP_MENU(WaveColorMenuTable)
   POPUP_MENU_RADIO_ITEM(OnInstrument1ID,
      GetWaveColorStr(0).Translation(), OnWaveColorChange)
   POPUP_MENU_RADIO_ITEM(OnInstrument2ID,
      GetWaveColorStr(1).Translation(), OnWaveColorChange)
   POPUP_MENU_RADIO_ITEM(OnInstrument3ID,
      GetWaveColorStr(2).Translation(), OnWaveColorChange)
   POPUP_MENU_RADIO_ITEM(OnInstrument4ID,
      GetWaveColorStr(3).Translation(), OnWaveColorChange)
END_POPUP_MENU()

/// Converts a WaveColor enumeration to a wxWidgets menu item Id.
int WaveColorMenuTable::IdOfWaveColor(int WaveColor)
{  return OnInstrument1ID + WaveColor;}

/// Handles the selection from the WaveColor submenu of the
/// track menu.
void WaveColorMenuTable::OnWaveColorChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= OnInstrument1ID && id <= OnInstrument4ID);
   const auto pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   int newWaveColor = id - OnInstrument1ID;

   AudacityProject *const project = ::GetActiveProject();

   for (auto channel : TrackList::Channels(pTrack))
      channel->SetWaveColorIndex(newWaveColor);

   ProjectHistory::Get( *project )
      .PushState(XO("Changed '%s' to %s")
         .Format( pTrack->GetName(), GetWaveColorStr(newWaveColor) ),
      XO("WaveColor Change"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}




//=============================================================================
// Table class for a sub-menu
class FormatMenuTable : public PopupMenuTable
{
   FormatMenuTable() : mpData(NULL) {}
   DECLARE_POPUP_MENU(FormatMenuTable);

public:
   static FormatMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = NULL;
   }

   PlayableTrackControls::InitMenuData *mpData;

   int IdOfFormat(int format);

   void OnFormatChange(wxCommandEvent & event);
};

FormatMenuTable &FormatMenuTable::Instance()
{
   static FormatMenuTable instance;
   return instance;
}

void FormatMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<PlayableTrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   auto formatId = IdOfFormat(pTrack->GetSampleFormat());
   SetMenuChecks(*pMenu, [=](int id){ return id == formatId; });

   AudacityProject *const project = ::GetActiveProject();
   bool unsafe = ProjectAudioIO::Get( *project ).IsAudioActive();
   for (int i = On16BitID; i <= OnFloatID; i++) {
      pMenu->Enable(i, !unsafe);
   }
}

BEGIN_POPUP_MENU(FormatMenuTable)
   POPUP_MENU_RADIO_ITEM(On16BitID,
      GetSampleFormatStr(int16Sample).Translation(), OnFormatChange)
   POPUP_MENU_RADIO_ITEM(On24BitID,
      GetSampleFormatStr(int24Sample).Translation(), OnFormatChange)
   POPUP_MENU_RADIO_ITEM(OnFloatID,
      GetSampleFormatStr(floatSample).Translation(), OnFormatChange)
END_POPUP_MENU()

/// Converts a format enumeration to a wxWidgets menu item Id.
int FormatMenuTable::IdOfFormat(int format)
{
   switch (format) {
   case int16Sample:
      return On16BitID;
   case int24Sample:
      return On24BitID;
   case floatSample:
      return OnFloatID;
   default:
      // ERROR -- should not happen
      wxASSERT(false);
      break;
   }
   return OnFloatID;// Compiler food.
}

/// Handles the selection from the Format submenu of the
/// track menu.
void FormatMenuTable::OnFormatChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= On16BitID && id <= OnFloatID);
   const auto pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   sampleFormat newFormat = int16Sample;

   switch (id) {
   case On16BitID:
      newFormat = int16Sample;
      break;
   case On24BitID:
      newFormat = int24Sample;
      break;
   case OnFloatID:
      newFormat = floatSample;
      break;
   default:
      // ERROR -- should not happen
      wxASSERT(false);
      break;
   }
   if (newFormat == pTrack->GetSampleFormat())
      return; // Nothing to do.

   AudacityProject *const project = ::GetActiveProject();

   for (auto channel : TrackList::Channels(pTrack))
      channel->ConvertToSampleFormat(newFormat);

   /* i18n-hint: The strings name a track and a format */
   ProjectHistory::Get( *project )
      .PushState(XO("Changed '%s' to %s")
         .Format( pTrack->GetName(), GetSampleFormatStr(newFormat) ),
      XO("Format Change"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}


//=============================================================================
// Table class for a sub-menu
class RateMenuTable : public PopupMenuTable
{
   RateMenuTable() : mpData(NULL) {}
   DECLARE_POPUP_MENU(RateMenuTable);

public:
   static RateMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = NULL;
   }

   PlayableTrackControls::InitMenuData *mpData;

   int IdOfRate(int rate);
   void SetRate(WaveTrack * pTrack, double rate);

   void OnRateChange(wxCommandEvent & event);
   void OnRateOther(wxCommandEvent & event);
};

RateMenuTable &RateMenuTable::Instance()
{
   static RateMenuTable instance;
   return instance;
}

void RateMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<PlayableTrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   const auto rateId = IdOfRate((int)pTrack->GetRate());
   SetMenuChecks(*pMenu, [=](int id){ return id == rateId; });

   AudacityProject *const project = ::GetActiveProject();
   bool unsafe = ProjectAudioIO::Get( *project ).IsAudioActive();
   for (int i = OnRate8ID; i <= OnRateOtherID; i++) {
      pMenu->Enable(i, !unsafe);
   }
}

// Because of Bug 1780 we can't use POPUP_MENU_RADIO_ITEM
// If we did, we'd get no message when clicking on Other...
// when it is already selected.
BEGIN_POPUP_MENU(RateMenuTable)
   POPUP_MENU_CHECK_ITEM(OnRate8ID, _("8000 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate11ID, _("11025 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate16ID, _("16000 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate22ID, _("22050 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate44ID, _("44100 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate48ID, _("48000 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate88ID, _("88200 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate96ID, _("96000 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate176ID, _("176400 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate192ID, _("192000 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate352ID, _("352800 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRate384ID, _("384000 Hz"), OnRateChange)
   POPUP_MENU_CHECK_ITEM(OnRateOtherID, _("&Other..."), OnRateOther)
END_POPUP_MENU()

const int nRates = 12;

///  gRates MUST CORRESPOND DIRECTLY TO THE RATES AS LISTED IN THE MENU!!
///  IN THE SAME ORDER!!
static int gRates[nRates] = { 8000, 11025, 16000, 22050, 44100, 48000, 88200, 96000,
176400, 192000, 352800, 384000 };

/// Converts a sampling rate to a wxWidgets menu item id
int RateMenuTable::IdOfRate(int rate)
{
   for (int i = 0; i<nRates; i++) {
      if (gRates[i] == rate)
         return i + OnRate8ID;
   }
   return OnRateOtherID;
}

/// Sets the sample rate for a track, and if it is linked to
/// another track, that one as well.
void RateMenuTable::SetRate(WaveTrack * pTrack, double rate)
{
   AudacityProject *const project = ::GetActiveProject();
   for (auto channel : TrackList::Channels(pTrack))
      channel->SetRate(rate);

   // Separate conversion of "rate" enables changing the decimals without affecting i18n
   wxString rateString = wxString::Format(wxT("%.3f"), rate);
   /* i18n-hint: The string names a track */
   ProjectHistory::Get( *project )
      .PushState(XO("Changed '%s' to %s Hz")
         .Format( pTrack->GetName(), rateString),
      XO("Rate Change"));
}

/// This method handles the selection from the Rate
/// submenu of the track menu, except for "Other" (/see OnRateOther).
void RateMenuTable::OnRateChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= OnRate8ID && id <= OnRate384ID);
   const auto pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   SetRate(pTrack, gRates[id - OnRate8ID]);

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

void RateMenuTable::OnRateOther(wxCommandEvent &)
{
   const auto pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   int newRate;

   /// \todo Remove artificial constants!!
   /// \todo Make a real dialog box out of this!!
   while (true)
   {
      wxDialogWrapper dlg(mpData->pParent, wxID_ANY, XO("Set Rate"));
      dlg.SetName();
      ShuttleGui S(&dlg, eIsCreating);
      wxString rate;
      wxComboBox *cb;

      rate.Printf(wxT("%ld"), lrint(pTrack->GetRate()));

      wxArrayStringEx rates{
         wxT("8000") ,
         wxT("11025") ,
         wxT("16000") ,
         wxT("22050") ,
         wxT("44100") ,
         wxT("48000") ,
         wxT("88200") ,
         wxT("96000") ,
         wxT("176400") ,
         wxT("192000") ,
         wxT("352800") ,
         wxT("384000") ,
      };

      S.StartVerticalLay(true);
      {
         S.SetBorder(10);
         S.StartHorizontalLay(wxEXPAND, false);
         {
            cb = S.AddCombo(_("New sample rate (Hz):"),
               rate,
               rates);
#if defined(__WXMAC__)
            // As of wxMac-2.8.12, setting manually is required
            // to handle rates not in the list.  See: Bug #427
            cb->SetValue(rate);
#endif
         }
         S.EndHorizontalLay();
         S.AddStandardButtons();
      }
      S.EndVerticalLay();

      dlg.SetClientSize(dlg.GetSizer()->CalcMin());
      dlg.Center();

      if (dlg.ShowModal() != wxID_OK)
      {
         return;  // user cancelled dialog
      }

      long lrate;
      if (cb->GetValue().ToLong(&lrate) && lrate >= 1 && lrate <= 1000000)
      {
         newRate = (int)lrate;
         break;
      }

      AudacityMessageBox(_("The entered value is invalid"), _("Error"),
         wxICON_ERROR, mpData->pParent);
   }

   SetRate(pTrack, newRate);

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

//=============================================================================
// Class defining common command handlers for mono and stereo tracks
class WaveTrackMenuTable : public PopupMenuTable
{
public:
   static WaveTrackMenuTable &Instance( Track * pTrack);
   Track * mpTrack;

protected:
   WaveTrackMenuTable() : mpData(NULL) {mpTrack=NULL;}

   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = nullptr;
   }

   DECLARE_POPUP_MENU(WaveTrackMenuTable);

   PlayableTrackControls::InitMenuData *mpData;

   void OnSetDisplay(wxCommandEvent & event);
   void OnSpectrogramSettings(wxCommandEvent & event);

   void OnChannelChange(wxCommandEvent & event);
   void OnMergeStereo(wxCommandEvent & event);

   // TODO: more-than-two-channels
   // How should we define generalized channel manipulation operations?
   void SplitStereo(bool stereo);

   void OnSwapChannels(wxCommandEvent & event);
   void OnSplitStereo(wxCommandEvent & event);
   void OnSplitStereoMono(wxCommandEvent & event);
};

WaveTrackMenuTable &WaveTrackMenuTable::Instance( Track * pTrack )
{
   static WaveTrackMenuTable instance;
   wxCommandEvent evt;
   // Clear it out so we force a repopulate
   instance.Invalidate( evt );
   // Ensure we know how to populate.
   // Messy, but the design does not seem to offer an alternative.
   // We won't use pTrack after populate.
   instance.mpTrack = pTrack;
   return instance;
}

void WaveTrackMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<PlayableTrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   std::vector<int> checkedIds;

   const auto displays = WaveTrackView::Get( *pTrack ).GetDisplays();
   for ( auto display : displays ) {
      checkedIds.push_back(
         display == WaveTrackViewConstants::Waveform
            ? (pTrack->GetWaveformSettings().isLinear()
               ? OnWaveformID : OnWaveformDBID)
            : OnSpectrumID);
   }

   // Bug 1253.  Shouldn't open preferences if audio is busy.
   // We can't change them on the fly yet anyway.
   auto gAudioIO = AudioIOBase::Get();
   const bool bAudioBusy = gAudioIO->IsBusy();
   bool hasSpectrum =
      make_iterator_range( displays.begin(), displays.end() )
         .contains( WaveTrackViewConstants::Spectrum );
   pMenu->Enable(OnSpectrogramSettingsID, hasSpectrum && !bAudioBusy);

   AudacityProject *const project = ::GetActiveProject();
   auto &tracks = TrackList::Get( *project );
   bool unsafe = RealtimeEffectManager::Get().RealtimeIsActive() &&
      ProjectAudioIO::Get( *project ).IsAudioActive();

   auto nChannels = TrackList::Channels(pTrack).size();
   const bool isMono = ( nChannels == 1 );
   const bool isStereo = ( nChannels == 2 );
   // Maybe more than stereo tracks some time?

   if ( isMono )
   {
      mpData = static_cast<PlayableTrackControls::InitMenuData*>(pUserData);
      WaveTrack *const pTrack2 = static_cast<WaveTrack*>(mpData->pTrack);

      auto next = * ++ tracks.Find(pTrack2);

      if (isMono) {
         const bool canMakeStereo =
            (next &&
             TrackList::Channels(next).size() == 1 &&
             track_cast<WaveTrack*>(next));

         pMenu->Enable(OnMergeStereoID, canMakeStereo && !unsafe);

         int itemId;
         switch (pTrack2->GetChannel()) {
            case Track::LeftChannel:
               itemId = OnChannelLeftID;
               break;
            case Track::RightChannel:
               itemId = OnChannelRightID;
               break;
            default:
               itemId = OnChannelMonoID;
               break;
         }
         checkedIds.push_back(itemId);
      }
   }
   else
   {
      pMenu->Enable(OnMergeStereoID, false);
   }

   SetMenuChecks(*pMenu, [&](int id){
      auto end = checkedIds.end();
      return end != std::find(checkedIds.begin(), end, id);
   });

   // Enable this only for properly stereo tracks:
   pMenu->Enable(OnSwapChannelsID, isStereo && !unsafe);
   pMenu->Enable(OnSplitStereoID, !isMono && !unsafe);

#ifndef EXPERIMENTAL_DA
   // Can be achieved by split stereo and then dragging pan slider.
   pMenu->Enable(OnSplitStereoMonoID, !isMono && !unsafe);
#endif

   // Several menu items no longer needed....
#if 0
   pMenu->Enable(OnChannelMonoID, isMono);
   pMenu->Enable(OnChannelLeftID, isMono);
   pMenu->Enable(OnChannelRightID, isMono);
#endif
}

BEGIN_POPUP_MENU(WaveTrackMenuTable)
   POPUP_MENU_SEPARATOR()

   // View types are now a non-exclusive choice.  The first two are mutually
   // exclusive, but the view may be in a state with either of those, and also
   // spectrogram, after a mouse drag.  Clicking any of these three makes that
   // view take up all the height.
   POPUP_MENU_CHECK_ITEM(OnWaveformID, _("Wa&veform"), OnSetDisplay)
   POPUP_MENU_CHECK_ITEM(OnWaveformDBID, _("&Waveform (dB)"), OnSetDisplay)
   POPUP_MENU_CHECK_ITEM(OnSpectrumID, _("&Spectrogram"), OnSetDisplay)

   POPUP_MENU_ITEM(OnSpectrogramSettingsID, _("S&pectrogram Settings..."), OnSpectrogramSettings)
   POPUP_MENU_SEPARATOR()

//   POPUP_MENU_RADIO_ITEM(OnChannelMonoID, _("&Mono"), OnChannelChange)
//   POPUP_MENU_RADIO_ITEM(OnChannelLeftID, _("&Left Channel"), OnChannelChange)
//   POPUP_MENU_RADIO_ITEM(OnChannelRightID, _("R&ight Channel"), OnChannelChange)
   POPUP_MENU_ITEM(OnMergeStereoID, _("Ma&ke Stereo Track"), OnMergeStereo)

   POPUP_MENU_ITEM(OnSwapChannelsID, _("Swap Stereo &Channels"), OnSwapChannels)
   POPUP_MENU_ITEM(OnSplitStereoID, _("Spl&it Stereo Track"), OnSplitStereo)
// DA: Uses split stereo track and then drag pan sliders for split-stereo-to-mono
#ifndef EXPERIMENTAL_DA
   POPUP_MENU_ITEM(OnSplitStereoMonoID, _("Split Stereo to Mo&no"), OnSplitStereoMono)
#endif

   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpTrack);
   if ( pTrack ) {
      const auto displays = WaveTrackView::Get( *pTrack ).GetDisplays();
      bool hasWaveform =
         make_iterator_range( displays.begin(), displays.end() )
            .contains( WaveTrackViewConstants::Waveform );
      if( hasWaveform ){
         POPUP_MENU_SEPARATOR()
         POPUP_MENU_SUB_MENU(OnWaveColorID, _("&Wave Color"), WaveColorMenuTable)
      }
   }

   POPUP_MENU_SEPARATOR()
   POPUP_MENU_SUB_MENU(0, _("&Format"), FormatMenuTable)
   POPUP_MENU_SEPARATOR()
   POPUP_MENU_SUB_MENU(0, _("Rat&e"), RateMenuTable)
END_POPUP_MENU()


///  Set the Display mode based on the menu choice in the Track Menu.
void WaveTrackMenuTable::OnSetDisplay(wxCommandEvent & event)
{
   using namespace WaveTrackViewConstants;
   int idInt = event.GetId();
   wxASSERT(idInt >= OnWaveformID && idInt <= OnSpectrumID);
   const auto pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   bool linear = false;
   WaveTrackView::WaveTrackDisplay id;
   switch (idInt) {
   default:
   case OnWaveformID:
      linear = true, id = Waveform; break;
   case OnWaveformDBID:
      id = Waveform; break;
   case OnSpectrumID:
      id = Spectrum; break;
   }

   const auto displays = WaveTrackView::Get( *pTrack ).GetDisplays();
   const bool wrongType = !(displays.size() == 1 && displays[0] == id);
   const bool wrongScale =
      (id == Waveform &&
      pTrack->GetWaveformSettings().isLinear() != linear);
   if (wrongType || wrongScale) {
      for (auto channel : TrackList::Channels(pTrack)) {
         channel->SetLastScaleType();
         WaveTrackView::Get( *channel )
            .SetDisplay(WaveTrackView::WaveTrackDisplay(id));
         if (wrongScale)
            channel->GetIndependentWaveformSettings().scaleType = linear
               ? WaveformSettings::stLinear
               : WaveformSettings::stLogarithmic;
      }

      AudacityProject *const project = ::GetActiveProject();
      ProjectHistory::Get( *project ).ModifyState(true);

      using namespace RefreshCode;
      mpData->result = RefreshAll | UpdateVRuler;
   }
}

void WaveTrackMenuTable::OnSpectrogramSettings(wxCommandEvent &)
{
   class ViewSettingsDialog final : public PrefsDialog
   {
   public:
      ViewSettingsDialog
         (wxWindow *parent, const wxString &title, PrefsDialog::Factories &factories,
         int page)
         : PrefsDialog(parent, title, factories)
         , mPage(page)
      {
      }

      long GetPreferredPage() override
      {
         return mPage;
      }

      void SavePreferredPage() override
      {
      }

   private:
      const int mPage;
   };

   auto gAudioIO = AudioIOBase::Get();
   if (gAudioIO->IsBusy()){
      AudacityMessageBox(_("To change Spectrogram Settings, stop any\n"
                     "playing or recording first."),
                   _("Stop the Audio First"), wxOK | wxICON_EXCLAMATION | wxCENTRE);
      return;
   }

   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   PrefsDialog::Factories factories;
   // factories.push_back(WaveformPrefsFactory( pTrack ));
   factories.push_back(SpectrumPrefsFactory( pTrack ));
   const int page =
      // (pTrack->GetDisplay() == WaveTrack::Spectrum) ? 1 :
      0;

   /* i18n-hint: An item name followed by a value, with appropriate separating punctuation */
   auto title = wxString::Format(_("%s: %s"), pTrack->GetName(), wxT(""));
   ViewSettingsDialog dialog(mpData->pParent, title, factories, page);

   if (0 != dialog.ShowModal()) {
      // Redraw
      AudacityProject *const project = ::GetActiveProject();
      ProjectHistory::Get( *project ).ModifyState(true);
      //Bug 1725 Toolbar was left greyed out.
      //This solution is overkill, but does fix the problem and is what the
      //prefs dialog normally does.
      MenuCreator::RebuildAllMenuBars();
      mpData->result = RefreshCode::RefreshAll;
   }
}

#if 0
void WaveTrackMenuTable::OnChannelChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= OnChannelLeftID && id <= OnChannelMonoID);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack);
   Track::ChannelType channel;
   wxString channelmsg;
   switch (id) {
   default:
   case OnChannelMonoID:
      channel = Track::MonoChannel;
      channelmsg = _("Mono");
      break;
   case OnChannelLeftID:
      channel = Track::LeftChannel;
      channelmsg = _("Left Channel");
      break;
   case OnChannelRightID:
      channel = Track::RightChannel;
      channelmsg = _("Right Channel");
      break;
   }
   pTrack->SetChannel(channel);
   AudacityProject *const project = ::GetActiveProject();
   /* i18n-hint: The strings name a track and a channel choice (mono, left, or right) */
   ProjectHistory::Get( *project )
      .PushState(wxString::Format(_("Changed '%s' to %s"),
         pTrack->GetName(),
         channelmsg),
         _("Channel"));
   mpData->result = RefreshCode::RefreshAll;
}
#endif

/// Merge two tracks into one stereo track ??
void WaveTrackMenuTable::OnMergeStereo(wxCommandEvent &)
{
   AudacityProject *const project = ::GetActiveProject();
   auto &tracks = TrackList::Get( *project );

   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack);

   auto partner = static_cast< WaveTrack * >
      ( *tracks.Find( pTrack ).advance( 1 ) );

   bool bBothMinimizedp =
      ((TrackView::Get( *pTrack ).GetMinimized()) &&
       (TrackView::Get( *partner ).GetMinimized()));

   tracks.GroupChannels( *pTrack, 2 );

   // Set partner's parameters to match target.
   partner->Merge(*pTrack);

   pTrack->SetPan( 0.0f );
   partner->SetPan( 0.0f );

   // Set NEW track heights and minimized state
   auto
      &view = WaveTrackView::Get( *pTrack ),
      &partnerView = WaveTrackView::Get( *partner );
   view.SetMinimized(false);
   partnerView.SetMinimized(false);
   int AverageHeight = (view.GetHeight() + partnerView.GetHeight()) / 2;
   view.SetHeight(AverageHeight);
   partnerView.SetHeight(AverageHeight);
   view.SetMinimized(bBothMinimizedp);
   partnerView.SetMinimized(bBothMinimizedp);

   partnerView.RestorePlacements( view.SavePlacements() );

   //On Demand - join the queues together.
   if (ODManager::IsInstanceCreated())
      if (!ODManager::Instance()
         ->MakeWaveTrackDependent(partner->SharedPointer<WaveTrack>(), pTrack))
      {
         ;
         //TODO: in the future, we will have to check the return value of MakeWaveTrackDependent -
         //if the tracks cannot merge, it returns false, and in that case we should not allow a merging.
         //for example it returns false when there are two different types of ODTasks on each track's queue.
         //we will need to display this to the user.
      }

   /* i18n-hint: The string names a track */
   ProjectHistory::Get( *project ).PushState(
      XO("Made '%s' a stereo track").Format( pTrack->GetName() ),
      XO("Make Stereo"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

/// Split a stereo track (or more-than-stereo?) into two (or more) tracks...
void WaveTrackMenuTable::SplitStereo(bool stereo)
{
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack);
   AudacityProject *const project = ::GetActiveProject();
   auto channels = TrackList::Channels( pTrack );

   int totalHeight = 0;
   int nChannels = 0;
   for (auto channel : channels) {
      // Keep original stereo track name.
      channel->SetName(pTrack->GetName());
      auto &view = TrackView::Get( *channel );
      if (stereo)
         channel->SetPanFromChannelType();

      //On Demand - have each channel add its own.
      if (ODManager::IsInstanceCreated())
         ODManager::Instance()->MakeWaveTrackIndependent(
            channel->SharedPointer<WaveTrack>() );
      //make sure no channel is smaller than its minimum height
      if (view.GetHeight() < view.GetMinimizedHeight())
         view.SetHeight(view.GetMinimizedHeight());
      totalHeight += view.GetHeight();
      ++nChannels;
   }

   TrackList::Get( *project ).GroupChannels( *pTrack, 1 );
   int averageHeight = totalHeight / nChannels;

   for (auto channel : channels)
      // Make tracks the same height
      TrackView::Get( *channel ).SetHeight( averageHeight );
}

/// Swap the left and right channels of a stero track...
void WaveTrackMenuTable::OnSwapChannels(wxCommandEvent &)
{
   AudacityProject *const project = ::GetActiveProject();

   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   auto channels = TrackList::Channels( pTrack );
   if (channels.size() != 2)
      return;

   auto &trackFocus = TrackFocus::Get( *project );
   Track *const focused = trackFocus.Get();
   const bool hasFocus = channels.contains( focused );

   auto partner = *channels.rbegin();

   SplitStereo(false);

   auto &tracks = TrackList::Get( *project );
   tracks.MoveUp( partner );
   tracks.GroupChannels( *partner, 2 );

   if (hasFocus)
      trackFocus.Set(partner);

   /* i18n-hint: The string names a track  */
   ProjectHistory::Get( *project ).PushState(
      XO("Swapped Channels in '%s'").Format( pTrack->GetName() ),
      XO("Swap Channels"));

   mpData->result = RefreshCode::RefreshAll;
}

/// Split a stereo track into two tracks...
void WaveTrackMenuTable::OnSplitStereo(wxCommandEvent &)
{
   SplitStereo(true);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   AudacityProject *const project = ::GetActiveProject();
   /* i18n-hint: The string names a track  */
   ProjectHistory::Get( *project ).PushState(
      XO("Split stereo track '%s'").Format( pTrack->GetName() ),
      XO("Split"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

/// Split a stereo track into two mono tracks...
void WaveTrackMenuTable::OnSplitStereoMono(wxCommandEvent &)
{
   SplitStereo(false);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   AudacityProject *const project = ::GetActiveProject();
   /* i18n-hint: The string names a track  */
   ProjectHistory::Get( *project ).PushState(
      XO("Split Stereo to Mono '%s'").Format( pTrack->GetName() ),
      XO("Split to Mono"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

//=============================================================================
PopupMenuTable *WaveTrackControls::GetMenuExtension(Track * pTrack)
{

   WaveTrackMenuTable & result = WaveTrackMenuTable::Instance( pTrack );
   return &result;
}

// drawing related
#include "../../../../widgets/ASlider.h"
#include "../../../../TrackInfo.h"
#include "../../../../TrackPanelDrawingContext.h"
#include "../../../../ViewInfo.h"

namespace {

void SliderDrawFunction
( LWSlider *(*Selector)
    (const wxRect &sliderRect, const WaveTrack *t, bool captured, wxWindow*),
  wxDC *dc, const wxRect &rect, const Track *pTrack,
  bool captured, bool highlight )
{
   wxRect sliderRect = rect;
   TrackInfo::GetSliderHorizontalBounds( rect.GetTopLeft(), sliderRect );
   auto wt = static_cast<const WaveTrack*>( pTrack );
   Selector( sliderRect, wt, captured, nullptr )->OnPaint(*dc, highlight);
}

void PanSliderDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto target = dynamic_cast<PanSliderHandle*>( context.target.get() );
   auto dc = &context.dc;
   bool hit = target && target->GetTrack().get() == pTrack;
   bool captured = hit && target->IsClicked();
   SliderDrawFunction
      ( &WaveTrackControls::PanSlider, dc, rect, pTrack, captured, hit);
}

void GainSliderDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto target = dynamic_cast<GainSliderHandle*>( context.target.get() );
   auto dc = &context.dc;
   bool hit = target && target->GetTrack().get() == pTrack;
   if( hit )
      hit=hit;
   bool captured = hit && target->IsClicked();
   SliderDrawFunction
      ( &WaveTrackControls::GainSlider, dc, rect, pTrack, captured, hit);
}

void StatusDrawFunction
   ( const TranslatableString &string, wxDC *dc, const wxRect &rect )
{
   static const int offset = 3;
   dc->DrawText(string.Translation(), rect.x + offset, rect.y);
}

void Status1DrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   auto wt = static_cast<const WaveTrack*>(pTrack);

   /// Returns the string to be displayed in the track label
   /// indicating whether the track is mono, left, right, or
   /// stereo and what sample rate it's using.
   auto rate = wt ? wt->GetRate() : 44100.0;
   TranslatableString s;
   if (!pTrack || TrackList::Channels(pTrack).size() > 1)
      // TODO: more-than-two-channels-message
      // more appropriate strings
      s = XO("Stereo, %dHz");
   else {
      if (wt->GetChannel() == Track::MonoChannel)
         s = XO("Mono, %dHz");
      else if (wt->GetChannel() == Track::LeftChannel)
         s = XO("Left, %dHz");
      else if (wt->GetChannel() == Track::RightChannel)
         s = XO("Right, %dHz");
   }
   s.Format( (int) (rate + 0.5) );

   StatusDrawFunction( s, dc, rect );
}

void Status2DrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   auto wt = static_cast<const WaveTrack*>(pTrack);
   auto format = wt ? wt->GetSampleFormat() : floatSample;
   auto s = GetSampleFormatStr(format);
   StatusDrawFunction( s, dc, rect );
}

}

using TCPLine = TrackInfo::TCPLine;

static const struct WaveTrackTCPLines
   : TCPLines { WaveTrackTCPLines() {
   (TCPLines&)*this =
      PlayableTrackControls::StaticTCPLines();
   insert( end(), {

      { TCPLine::kItemGain, kTrackInfoSliderHeight, kTrackInfoSliderExtra,
        GainSliderDrawFunction },
      { TCPLine::kItemPan, kTrackInfoSliderHeight, kTrackInfoSliderExtra,
        PanSliderDrawFunction },

#ifdef EXPERIMENTAL_DA
      // DA: Does not have status information for a track.
#else
      { TCPLine::kItemStatusInfo1, 12, 0,
        Status1DrawFunction },
      { TCPLine::kItemStatusInfo2, 12, 0,
        Status2DrawFunction },
#endif

   } );
} } waveTrackTCPLines;

void WaveTrackControls::GetGainRect(const wxPoint &topleft, wxRect & dest)
{
   TrackInfo::GetSliderHorizontalBounds( topleft, dest );
   auto results = CalcItemY( waveTrackTCPLines, TCPLine::kItemGain );
   dest.y = topleft.y + results.first;
   dest.height = results.second;
}

void WaveTrackControls::GetPanRect(const wxPoint &topleft, wxRect & dest)
{
   GetGainRect( topleft, dest );
   auto results = CalcItemY( waveTrackTCPLines, TCPLine::kItemPan );
   dest.y = topleft.y + results.first;
}

unsigned WaveTrackControls::DefaultWaveTrackHeight()
{
   return TrackInfo::DefaultTrackHeight( waveTrackTCPLines );
}

const TCPLines &WaveTrackControls::GetTCPLines() const
{
   return waveTrackTCPLines;
}

namespace
{
std::unique_ptr<LWSlider>
   gGainCaptured
   , gPanCaptured
   , gGain
   , gPan;
}

LWSlider *WaveTrackControls::GainSlider(
   CellularPanel &panel, const WaveTrack &wt )
{
   auto &controls = TrackControls::Get( wt );
   auto rect = panel.FindRect( controls );
   wxRect sliderRect;
   GetGainRect( rect.GetTopLeft(), sliderRect );
   return GainSlider( sliderRect, &wt, false, &panel );
}

LWSlider * WaveTrackControls::GainSlider
(const wxRect &sliderRect, const WaveTrack *t, bool captured, wxWindow *pParent)
{
   static std::once_flag flag;
   std::call_once( flag, [] {
      wxCommandEvent dummy;
      ReCreateGainSlider( dummy );
      wxTheApp->Bind(EVT_THEME_CHANGE, ReCreateGainSlider);
   } );

   wxPoint pos = sliderRect.GetPosition();
   float gain = t ? t->GetGain() : 1.0;

   gGain->Move(pos);
   gGain->Set(gain);
   gGainCaptured->Move(pos);
   gGainCaptured->Set(gain);

   auto slider = (captured ? gGainCaptured : gGain).get();
   slider->SetParent( pParent ? pParent :
      FindProjectFrame( ::GetActiveProject() ) );
   return slider;
}

void WaveTrackControls::ReCreateGainSlider( wxEvent &event )
{
   event.Skip();

   const wxPoint point{ 0, 0 };
   wxRect sliderRect;
   GetGainRect(point, sliderRect);

   float defPos = 1.0;
   /* i18n-hint: Title of the Gain slider, used to adjust the volume */
   gGain = std::make_unique<LWSlider>(nullptr, XO("Gain"),
                        wxPoint(sliderRect.x, sliderRect.y),
                        wxSize(sliderRect.width, sliderRect.height),
                        DB_SLIDER);
   gGain->SetDefaultValue(defPos);

   gGainCaptured = std::make_unique<LWSlider>(nullptr, XO("Gain"),
                                wxPoint(sliderRect.x, sliderRect.y),
                                wxSize(sliderRect.width, sliderRect.height),
                                DB_SLIDER);
   gGainCaptured->SetDefaultValue(defPos);
}

LWSlider *WaveTrackControls::PanSlider(
   CellularPanel &panel, const WaveTrack &wt )
{
   auto &controls = TrackControls::Get( wt );
   auto rect = panel.FindRect( controls );
   wxRect sliderRect;
   GetPanRect( rect.GetTopLeft(), sliderRect );
   return PanSlider( sliderRect, &wt, false,  &panel );
}

LWSlider * WaveTrackControls::PanSlider
(const wxRect &sliderRect, const WaveTrack *t, bool captured, wxWindow *pParent)
{
   static std::once_flag flag;
   std::call_once( flag, [] {
      wxCommandEvent dummy;
      ReCreatePanSlider( dummy );
      wxTheApp->Bind(EVT_THEME_CHANGE, ReCreatePanSlider);
   } );

   wxPoint pos = sliderRect.GetPosition();
   float pan = t ? t->GetPan() : 0.0;

   gPan->Move(pos);
   gPan->Set(pan);
   gPanCaptured->Move(pos);
   gPanCaptured->Set(pan);

   auto slider = (captured ? gPanCaptured : gPan).get();
   slider->SetParent( pParent ? pParent :
      FindProjectFrame( ::GetActiveProject() ) );
   return slider;
}

void WaveTrackControls::ReCreatePanSlider( wxEvent &event )
{
   event.Skip();

   const wxPoint point{ 0, 0 };
   wxRect sliderRect;
   GetPanRect(point, sliderRect);

   float defPos = 0.0;
   /* i18n-hint: Title of the Pan slider, used to move the sound left or right */
   gPan = std::make_unique<LWSlider>(nullptr, XO("Pan"),
                       wxPoint(sliderRect.x, sliderRect.y),
                       wxSize(sliderRect.width, sliderRect.height),
                       PAN_SLIDER);
   gPan->SetDefaultValue(defPos);

   gPanCaptured = std::make_unique<LWSlider>(nullptr, XO("Pan"),
                               wxPoint(sliderRect.x, sliderRect.y),
                               wxSize(sliderRect.width, sliderRect.height),
                               PAN_SLIDER);
   gPanCaptured->SetDefaultValue(defPos);
}

using DoGetWaveTrackControls = DoGetControls::Override< WaveTrack >;
template<> template<> auto DoGetWaveTrackControls::Implementation() -> Function {
   return [](WaveTrack &track) {
      return std::make_shared<WaveTrackControls>( track.SharedPointer() );
   };
}
static DoGetWaveTrackControls registerDoGetWaveTrackControls;

using GetDefaultWaveTrackHeight = GetDefaultTrackHeight::Override< WaveTrack >;
template<> template<>
auto GetDefaultWaveTrackHeight::Implementation() -> Function {
   return [](WaveTrack &) {
      return WaveTrackControls::DefaultWaveTrackHeight();
   };
}
static GetDefaultWaveTrackHeight registerGetDefaultWaveTrackHeight;
