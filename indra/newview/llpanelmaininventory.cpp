/**
 * @file llpanelmaininventory.cpp
 * @brief Implementation of llpanelmaininventory.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llpanelmaininventory.h"

#include "llagent.h"
#include "llagentbenefits.h"
#include "llagentcamera.h"
#include "llavataractions.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lldndbutton.h"
#include "llfilepicker.h"
#include "llinventorybridge.h"
#include "llinventoryfunctions.h"
#include "llinventorygallery.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llfiltereditor.h"
#include "llfloatersidepanelcontainer.h"
#include "llfloaterreg.h"
#include "llmenubutton.h"
#include "lloutfitobserver.h"
#include "llpanelmarketplaceinbox.h"
#include "llpreviewtexture.h"
#include "llresmgr.h"
#include "llscrollcontainer.h"
#include "llsdserialize.h"
#include "llsdparam.h"
#include "llspinctrl.h"
#include "lltoggleablemenu.h"
#include "lltooldraganddrop.h"
#include "lltrans.h"
#include "llviewermenu.h"
#include "llviewertexturelist.h"
#include "llviewerinventory.h"
#include "llsidepanelinventory.h"
#include "llfolderview.h"
#include "llradiogroup.h"
#include "llenvironment.h"
#include "llweb.h"

// <FS:AW opensim currency support>
#include "lltrans.h"
#include "llviewernetwork.h"
// </FS:AW opensim currency support>
#include "fscommon.h"

// <FS:Ansariel> FIRE-12808: Don't save filters during settings restore
bool LLPanelMainInventory::sSaveFilters = true;

const std::string FILTERS_FILENAME("filters.xml");

const std::string ALL_ITEMS("All Items");
const std::string RECENT_ITEMS("Recent Items");
const std::string WORN_ITEMS("Worn Items");

static LLPanelInjector<LLPanelMainInventory> t_inventory("panel_main_inventory");

///----------------------------------------------------------------------------
/// LLFloaterInventoryFinder
///----------------------------------------------------------------------------

class LLFloaterInventoryFinder : public LLFloater
{
public:
    LLFloaterInventoryFinder( LLPanelMainInventory* inventory_view);
    virtual void draw();
    /*virtual*/ bool    postBuild();
    void changeFilter(LLInventoryFilter* filter);
    void updateElementsFromFilter();
    bool getCheckShowEmpty();
    bool getCheckSinceLogoff();
    U32 getDateSearchDirection();

    void onCreatorSelfFilterCommit();
    void onCreatorOtherFilterCommit();

    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    void onOnlyCoalescedFilterCommit(); // Commit method for the Only Coalesced Filter checkbox
    void onShowLinksFilterCommit(); // Commit method for the Show Links Filter combo box
    // </FS:minerjr> [FIRE-35042]

    void onPermissionsChanged(); // <FS:Zi> FIRE-1175 - Filter Permissions Menu

    static void onTimeAgo(LLUICtrl*, void *);
    static void onCloseBtn(void* user_data);
    static void selectAllTypes(void* user_data);
    static void selectNoTypes(void* user_data);

    // <FS:Ansariel> FIRE-5160: Don't reset inventory filter when clearing search term
    void onResetBtn();
private:
    LLPanelMainInventory*   mPanelMainInventory;
    LLSpinCtrl*         mSpinSinceDays;
    LLSpinCtrl*         mSpinSinceHours;
    LLCheckBoxCtrl*     mCreatorSelf;
    LLCheckBoxCtrl*     mCreatorOthers;
    LLInventoryFilter*  mFilter;
    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    LLCheckBoxCtrl*     mOnlyCoalescedFilterCheck; // Stores the pointer to the Only Coalesced filter checkbox
    LLComboBox*         mShowLinksFilterCombo; // Stores the pointer to the Show Links filter combo box
    // </FS:minerjr> [FIRE-35042]
};

///----------------------------------------------------------------------------
/// LLPanelMainInventory
///----------------------------------------------------------------------------

LLPanelMainInventory::LLPanelMainInventory(const LLPanel::Params& p)
    : LLPanel(p),
      mActivePanel(NULL),
      mWornItemsPanel(NULL),
      mSavedFolderState(NULL),
      mFilterText(""),
      mMenuGearDefault(NULL),
      mMenuVisibility(NULL),
      mMenuAddHandle(),
      mNeedUploadCost(true),
      mMenuViewDefault(NULL),
      mSingleFolderMode(false),
      mForceShowInvLayout(false),
      mViewMode(MODE_COMBINATION),
      mListViewRootUpdatedConnection(),
      mGalleryRootUpdatedConnection(),
      mViewMenuButton(nullptr), // <FS:Ansariel> Keep better inventory layout
      mSearchTypeCombo(NULL) // <FS:Ansariel> Properly initialize this
{
    // Menu Callbacks (non contex menus)
    mCommitCallbackRegistrar.add("Inventory.DoToSelected", boost::bind(&LLPanelMainInventory::doToSelected, this, _2));
    mCommitCallbackRegistrar.add("Inventory.CloseAllFolders", boost::bind(&LLPanelMainInventory::closeAllFolders, this));
    mCommitCallbackRegistrar.add("Inventory.EmptyTrash", boost::bind(&LLInventoryModel::emptyFolderType, &gInventory, "ConfirmEmptyTrash", LLFolderType::FT_TRASH));
    mCommitCallbackRegistrar.add("Inventory.EmptyLostAndFound", boost::bind(&LLInventoryModel::emptyFolderType, &gInventory, "ConfirmEmptyLostAndFound", LLFolderType::FT_LOST_AND_FOUND));
    mCommitCallbackRegistrar.add("Inventory.DoCreate", boost::bind(&LLPanelMainInventory::doCreate, this, _2));
    mCommitCallbackRegistrar.add("Inventory.ShowFilters", boost::bind(&LLPanelMainInventory::toggleFindOptions, this));
    mCommitCallbackRegistrar.add("Inventory.ResetFilters", boost::bind(&LLPanelMainInventory::resetFilters, this));
    //mCommitCallbackRegistrar.add("Inventory.SetSortBy", boost::bind(&LLPanelMainInventory::setSortBy, this, _2)); // <FS:Zi> Sort By menu handlers

    mEnableCallbackRegistrar.add("Inventory.EnvironmentEnabled", [](LLUICtrl *, const LLSD &) { return LLPanelMainInventory::hasSettingsInventory(); });
    mEnableCallbackRegistrar.add("Inventory.MaterialsEnabled", [](LLUICtrl *, const LLSD &) { return LLPanelMainInventory::hasMaterialsInventory(); });


    // <FS:Zi> Filter Links Menu
    mCommitCallbackRegistrar.add("Inventory.FilterLinks.Set", boost::bind(&LLPanelMainInventory::onFilterLinksChecked, this, _2));
    mEnableCallbackRegistrar.add("Inventory.FilterLinks.Check", boost::bind(&LLPanelMainInventory::isFilterLinksChecked, this, _2));
    // </FS:Zi> Filter Links Menu

    // <FS:Zi> FIRE-1175 - Filter Permissions Menu
    mCommitCallbackRegistrar.add("Inventory.FilterPermissions.Set", boost::bind(&LLPanelMainInventory::onFilterPermissionsChecked, this, _2));
    mEnableCallbackRegistrar.add("Inventory.FilterPermissions.Check", boost::bind(&LLPanelMainInventory::isFilterPermissionsChecked, this, _2));
    // </FS:Zi>

    // <FS:Zi> Extended Inventory Search
    mCommitCallbackRegistrar.add("Inventory.SearchType.Set", boost::bind(&LLPanelMainInventory::onSearchTypeChecked, this, _2));
    mEnableCallbackRegistrar.add("Inventory.SearchType.Check", boost::bind(&LLPanelMainInventory::isSearchTypeChecked, this, _2));
    // </FS:Zi> Extended Inventory Search

    // <FS:Zi> Sort By menu handlers
    // we set up our own handlers here because the gear menu handlers are only set up
    // later in the code, so our XML based menus can't reach them yet.
    mCommitCallbackRegistrar.add("Inventory.SortBy.Set", boost::bind(&LLPanelMainInventory::setSortBy, this, _2));
    mEnableCallbackRegistrar.add("Inventory.SortBy.Check", boost::bind(&LLPanelMainInventory::isSortByChecked, this, _2));
    // </FS:Zi> Sort By menu handlers

    // <FS:Ansariel> Add handler for being able to directly route to onCustomAction
    mCommitCallbackRegistrar.add("Inventory.CustomAction", boost::bind(&LLPanelMainInventory::onCustomAction, this, _2));

    // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
    mCommitCallbackRegistrar.add("Inventory.CoalescedObjects.Toggle", boost::bind(&LLPanelMainInventory::onCoalescedObjectsToggled, this, _2));
    mEnableCallbackRegistrar.add("Inventory.CoalescedObjects.Check", boost::bind(&LLPanelMainInventory::isCoalescedObjectsChecked, this, _2));
    // </FS:Zi>

    // <FS:Ansariel> Register all callback handlers early
    mCommitCallbackRegistrar.add("Inventory.GearDefault.Custom.Action", boost::bind(&LLPanelMainInventory::onCustomAction, this, _2));
    mEnableCallbackRegistrar.add("Inventory.GearDefault.Check", boost::bind(&LLPanelMainInventory::isActionChecked, this, _2));
    mEnableCallbackRegistrar.add("Inventory.GearDefault.Enable", boost::bind(&LLPanelMainInventory::isActionEnabled, this, _2));
    mEnableCallbackRegistrar.add("Inventory.GearDefault.Visible", boost::bind(&LLPanelMainInventory::isActionVisible, this, _2));
    // </FS:Ansariel>

    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    // Added new button to show all filters, not just Only Coalesced Filter
    // Add a call back on the existing Inventory ShowFilters Check on the show_filters_inv_btn
    mEnableCallbackRegistrar.add("Inventory.ShowFilters.Check", boost::bind(&LLPanelMainInventory::isAnyFilterChecked, this, _2));
    // </FS:minerjr> [FIRE-35042]

    mSavedFolderState = new LLSaveFolderState();
    mSavedFolderState->setApply(false);

    // <FS:Zi> Filter dropdown
    // create name-to-number mapping for the dropdown filter
    mFilterMap["filter_type_animations"]    = 0x01 << LLInventoryType::IT_ANIMATION;
    mFilterMap["filter_type_calling_cards"] = 0x01 << LLInventoryType::IT_CALLINGCARD;
    mFilterMap["filter_type_clothing"]      = 0x01 << LLInventoryType::IT_WEARABLE;
    mFilterMap["filter_type_gestures"]      = 0x01 << LLInventoryType::IT_GESTURE;
    mFilterMap["filter_type_landmarks"]     = 0x01 << LLInventoryType::IT_LANDMARK;
    mFilterMap["filter_type_notecards"]     = 0x01 << LLInventoryType::IT_NOTECARD;
    mFilterMap["filter_type_objects"]       = 0x01 << LLInventoryType::IT_OBJECT;
    mFilterMap["filter_type_scripts"]       = 0x01 << LLInventoryType::IT_LSL;
    mFilterMap["filter_type_sounds"]        = 0x01 << LLInventoryType::IT_SOUND;
    mFilterMap["filter_type_textures"]      = 0x01 << LLInventoryType::IT_TEXTURE;
    mFilterMap["filter_type_snapshots"]     = 0x01 << LLInventoryType::IT_SNAPSHOT;
    mFilterMap["filter_type_settings"]      = 0x01 << LLInventoryType::IT_SETTINGS;
    mFilterMap["filter_type_materials"]     = 0x01 << LLInventoryType::IT_MATERIAL;
// <AS:chanayane> Search folders only
    mFilterMap["filter_type_folders"]       = 0x01 << LLInventoryType::IT_CATEGORY;
// </AS:chanayane>

    // initialize empty filter mask
    mFilterMask = 0;
    // add filter bits to the mask
    for (std::map<std::string, U64>::iterator it = mFilterMap.begin() ; it != mFilterMap.end(); ++it)
    {
        mFilterMask |= (*it).second;
    }
    // </FS:Zi> Filter dropdown
}

bool LLPanelMainInventory::postBuild()
{
    gInventory.addObserver(this);

    // <FS:Zi> Inventory Collapse and Expand Buttons
    mCollapseBtn = getChild<LLButton>("collapse_btn");
    mCollapseBtn->setClickedCallback(boost::bind(&LLPanelMainInventory::onCollapseButtonClicked, this));

    mExpandBtn = getChild<LLButton>("expand_btn");
    mExpandBtn->setClickedCallback(boost::bind(&LLPanelMainInventory::onExpandButtonClicked, this));
    // </FS:Zi> Inventory Collapse and Expand Buttons

    mFilterTabs = getChild<LLTabContainer>("inventory filter tabs");
    mFilterTabs->setCommitCallback(boost::bind(&LLPanelMainInventory::onFilterSelected, this));

    mCounterCtrl = getChild<LLUICtrl>("ItemcountText");

    //panel->getFilter().markDefault();

    // Set up the default inv. panel/filter settings.
    mAllItemsPanel = getChild<LLInventoryPanel>(ALL_ITEMS);
    if (mAllItemsPanel)
    {
        // "All Items" is the previous only view, so it gets the InventorySortOrder
        mAllItemsPanel->setSortOrder(gSavedSettings.getU32(LLInventoryPanel::DEFAULT_SORT_ORDER));
        mAllItemsPanel->getFilter().markDefault();
        mAllItemsPanel->getRootFolder()->applyFunctorRecursively(*mSavedFolderState);
        mAllItemsPanel->setSelectCallback(boost::bind(&LLPanelMainInventory::onSelectionChange, this, mAllItemsPanel, _1, _2));
        mResortActivePanel = true;
    }
    mActivePanel = mAllItemsPanel;

    mRecentPanel = getChild<LLInventoryPanel>(RECENT_ITEMS);
    if (mRecentPanel)
    {
        // assign default values until we will be sure that we have setting to restore
        mRecentPanel->setSinceLogoff(true);
        // <FS:Zi> Recent items panel should save sort order
        // mRecentPanel->setSortOrder(LLInventoryFilter::SO_DATE);
        mRecentPanel->setSortOrder(gSavedSettings.getU32(LLInventoryPanel::RECENTITEMS_SORT_ORDER));
        // </FS:Zi>
        mRecentPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
        LLInventoryFilter& recent_filter = mRecentPanel->getFilter();
        recent_filter.setFilterObjectTypes(recent_filter.getFilterObjectTypes() & ~(0x1 << LLInventoryType::IT_CATEGORY));
        recent_filter.setEmptyLookupMessage("InventoryNoMatchingRecentItems");
        recent_filter.markDefault();
        mRecentPanel->setSelectCallback(boost::bind(&LLPanelMainInventory::onSelectionChange, this, mRecentPanel, _1, _2));
    }

    mWornItemsPanel = getChild<LLInventoryPanel>(WORN_ITEMS);
    if (mWornItemsPanel)
    {
        U32 filter_types = 0x0;
        filter_types |= 0x1 << LLInventoryType::IT_WEARABLE;
        filter_types |= 0x1 << LLInventoryType::IT_ATTACHMENT;
        filter_types |= 0x1 << LLInventoryType::IT_OBJECT;
        mWornItemsPanel->setFilterTypes(filter_types);
        mWornItemsPanel->setFilterWorn();
        mWornItemsPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
        mWornItemsPanel->setFilterLinks(LLInventoryFilter::FILTERLINK_EXCLUDE_LINKS);
        LLInventoryFilter& worn_filter = mWornItemsPanel->getFilter();
        worn_filter.setFilterCategoryTypes(worn_filter.getFilterCategoryTypes() | (1ULL << LLFolderType::FT_INBOX));
        worn_filter.markDefault();
        mWornItemsPanel->setSelectCallback(boost::bind(&LLPanelMainInventory::onSelectionChange, this, mWornItemsPanel, _1, _2));

        // <FS:Ansariel> Firestorm additions
        mWornItemsPanel->setSortOrder(gSavedSettings.getU32(LLInventoryPanel::DEFAULT_SORT_ORDER));

        if (mWornItemsPanel->getRootFolder())
        {
            mWornItemsPanel->getRootFolder()->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_NO);
            mWornItemsPanel->getRootFolder()->arrangeAll();
        }
        // </FS:Ansariel>
    }
    // <FS:Ansariel> Only if we actually have it!
    //mSearchTypeCombo  = getChild<LLComboBox>("search_type");
    mSearchTypeCombo  = findChild<LLComboBox>("search_type");
    // <FS:Ansariel>
    if(mSearchTypeCombo)
    {
        mSearchTypeCombo->setCommitCallback(boost::bind(&LLPanelMainInventory::onSelectSearchType, this));
    }
    // Now load the stored settings from disk, if available.
    std::string filterSaveName(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, FILTERS_FILENAME));
    LL_INFOS("Inventory") << "LLPanelMainInventory::init: reading from " << filterSaveName << LL_ENDL;
    llifstream file(filterSaveName.c_str());
    LLSD savedFilterState;
    if (file.is_open())
    {
        LLSDSerialize::fromXML(savedFilterState, file);
        file.close();

        // Load the persistent "Recent Items" settings.
        // Note that the "All Items" settings do not persist.
        if(mRecentPanel)
        {
            if(savedFilterState.has(mRecentPanel->getFilter().getName()))
            {
                LLSD recent_items = savedFilterState.get(
                    mRecentPanel->getFilter().getName());
                // <FS:Ansariel> Fix wrong param type
                //LLInventoryFilter::Params p;
                LLInventoryPanel::InventoryState p;
                // </FS:Ansariel>
                LLParamSDParser parser;
                parser.readSD(recent_items, p);
                // <FS:Ansariel> Fix wrong param type
                //mRecentPanel->getFilter().fromParams(p);
                mRecentPanel->getFilter().fromParams(p.filter);
                // </FS:Ansariel>
                // <FS:Ansariel> We do that earlier already
                //mRecentPanel->setSortOrder(gSavedSettings.getU32(LLInventoryPanel::RECENTITEMS_SORT_ORDER));

                // </FS:Ansariel> Recent items panel doesn't filter empty folders until filter floater has been opened
                LLInventoryFilter& recent_filter = mRecentPanel->getFilter();
                recent_filter.setFilterObjectTypes(recent_filter.getFilterObjectTypes() & ~(0x1 << LLInventoryType::IT_CATEGORY));
                // </FS:Ansariel>
            }
        }
        if(mActivePanel)
        {
            if(savedFilterState.has(mActivePanel->getFilter().getName()))
            {
                LLSD items = savedFilterState.get(mActivePanel->getFilter().getName());
                LLInventoryFilter::Params p;
                LLParamSDParser parser;
                parser.readSD(items, p);
                mActivePanel->getFilter().setSearchVisibilityTypes(p);
            }
        }

    }

    mFilterEditor = getChild<LLFilterEditor>("inventory search editor");
    if (mFilterEditor)
    {
        mFilterEditor->setCommitCallback(boost::bind(&LLPanelMainInventory::onFilterEdit, this, _2));
    }

    // <FS:Zi> Filter dropdown
    mFilterComboBox = getChild<LLComboBox>("filter_combo_box");
    mFilterComboBox->setCommitCallback(boost::bind(&LLPanelMainInventory::onFilterTypeSelected, this, _2));
    // </FS:Zi> Filter dropdown

    mGearMenuButton = getChild<LLMenuButton>("options_gear_btn");
    mVisibilityMenuButton = getChild<LLMenuButton>("options_visibility_btn");
    // <FS:Ansariel> Keep better inventory layout
    //mViewMenuButton = getChild<LLMenuButton>("view_btn");
    mViewMenuButton = findChild<LLMenuButton>("view_btn");

    mBackBtn = getChild<LLButton>("back_btn");
    mForwardBtn = getChild<LLButton>("forward_btn");
    mUpBtn = getChild<LLButton>("up_btn");
    mViewModeBtn = getChild<LLButton>("view_mode_btn");
    mNavigationBtnsPanel = getChild<LLLayoutPanel>("nav_buttons");

    mDefaultViewPanel = getChild<LLPanel>("default_inventory_panel");
    mCombinationViewPanel = getChild<LLPanel>("combination_view_inventory");
    mCombinationGalleryLayoutPanel = getChild<LLLayoutPanel>("comb_gallery_layout");
    mCombinationListLayoutPanel = getChild<LLLayoutPanel>("comb_inventory_layout");
    mCombinationLayoutStack = getChild<LLLayoutStack>("combination_view_stack");

    mCombinationInventoryPanel = getChild<LLInventorySingleFolderPanel>("comb_single_folder_inv");
    LLInventoryFilter& comb_inv_filter = mCombinationInventoryPanel->getFilter();
    comb_inv_filter.setFilterThumbnails(LLInventoryFilter::FILTER_EXCLUDE_THUMBNAILS);
    comb_inv_filter.markDefault();
    mCombinationInventoryPanel->setSelectCallback(boost::bind(&LLPanelMainInventory::onCombinationInventorySelectionChanged, this, _1, _2));
    mListViewRootUpdatedConnection = mCombinationInventoryPanel->setRootChangedCallback(boost::bind(&LLPanelMainInventory::onCombinationRootChanged, this, false));

    mCombinationGalleryPanel = getChild<LLInventoryGallery>("comb_gallery_view_inv");
    mCombinationGalleryPanel->setSortOrder(mCombinationInventoryPanel->getSortOrder());
    LLInventoryFilter& comb_gallery_filter = mCombinationGalleryPanel->getFilter();
    comb_gallery_filter.setFilterThumbnails(LLInventoryFilter::FILTER_ONLY_THUMBNAILS);
    comb_gallery_filter.markDefault();
    mGalleryRootUpdatedConnection = mCombinationGalleryPanel->setRootChangedCallback(boost::bind(&LLPanelMainInventory::onCombinationRootChanged, this, true));
    mCombinationGalleryPanel->setSelectionChangeCallback(boost::bind(&LLPanelMainInventory::onCombinationGallerySelectionChanged, this, _1));

    initListCommandsHandlers();
    const std::string sound_upload_cost_str = std::to_string(LLAgentBenefitsMgr::current().getSoundUploadCost());
    const std::string animation_upload_cost_str = std::to_string(LLAgentBenefitsMgr::current().getAnimationUploadCost());

    LLMenuGL* menu = (LLMenuGL*)mMenuAddHandle.get();
    if (menu)
    {
        menu->getChild<LLMenuItemGL>("Upload Sound")->setLabelArg("[COST]", sound_upload_cost_str);
        menu->getChild<LLMenuItemGL>("Upload Animation")->setLabelArg("[COST]", animation_upload_cost_str);
    }

    // Trigger callback for focus received so we can deselect items in inbox/outbox
    LLFocusableElement::setFocusReceivedCallback(boost::bind(&LLPanelMainInventory::onFocusReceived, this));

    return true;
}

// Destroys the object
LLPanelMainInventory::~LLPanelMainInventory( void )
{
    // Save the filters state.
    // Some params types cannot be saved this way
    // for example, LLParamSDParser doesn't know about U64,
    // so some FilterOps params should be revised.
    LLSD filterRoot;
    if (mAllItemsPanel)
    {
        LLSD filterState;
        LLInventoryPanel::InventoryState p;
        mAllItemsPanel->getFilter().toParams(p.filter);
        mAllItemsPanel->getRootViewModel().getSorter().toParams(p.sort);
        if (p.validateBlock(false))
        {
            LLParamSDParser().writeSD(filterState, p);
            filterRoot[mAllItemsPanel->getName()] = filterState;
        }
    }

    if (mRecentPanel)
    {
        LLSD filterState;
        LLInventoryPanel::InventoryState p;
        mRecentPanel->getFilter().toParams(p.filter);
        mRecentPanel->getRootViewModel().getSorter().toParams(p.sort);
        if (p.validateBlock(false))
        {
            LLParamSDParser().writeSD(filterState, p);
            filterRoot[mRecentPanel->getName()] = filterState;
        }
    }

    // <FS:Ansariel> FIRE-12808: Don't save filters during settings restore
    if (sSaveFilters)
    {
    // </FS:Ansariel>
    std::string filterSaveName(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, FILTERS_FILENAME));
    llofstream filtersFile(filterSaveName.c_str());
    if(!LLSDSerialize::toPrettyXML(filterRoot, filtersFile))
    {
        LL_WARNS() << "Could not write to filters save file " << filterSaveName << LL_ENDL;
    }
    else
    {
        filtersFile.close();
    }
    // <FS:Ansariel> FIRE-12808: Don't save filters during settings restore
    }
    // </FS:Ansariel>

    gInventory.removeObserver(this);
    delete mSavedFolderState;

    auto menu = mMenuAddHandle.get();
    if(menu)
    {
        menu->die();
        mMenuAddHandle.markDead();
    }

    if (mListViewRootUpdatedConnection.connected())
    {
        mListViewRootUpdatedConnection.disconnect();
    }
    if (mGalleryRootUpdatedConnection.connected())
    {
        mGalleryRootUpdatedConnection.disconnect();
    }
}

LLInventoryPanel* LLPanelMainInventory::getAllItemsPanel()
{
    return  mAllItemsPanel;
}

void LLPanelMainInventory::selectAllItemsPanel()
{
    mFilterTabs->selectFirstTab();
}

bool LLPanelMainInventory::isRecentItemsPanelSelected()
{
    return (mRecentPanel == getActivePanel());
}

void LLPanelMainInventory::startSearch()
{
    // this forces focus to line editor portion of search editor
    if (mFilterEditor)
    {
        mFilterEditor->focusFirstItem(true);
    }
}

bool LLPanelMainInventory::handleKeyHere(KEY key, MASK mask)
{
    // <FS:Ansariel> CTRL-F focusses local search editor
    if (FSCommon::isFilterEditorKeyCombo(key, mask))
    {
        mFilterEditor->setFocus(true);
        return true;
    }
    // </FS:Ansariel>

    LLFolderView* root_folder = mActivePanel ? mActivePanel->getRootFolder() : NULL;
    if (root_folder)
    {
        // first check for user accepting current search results
        if (mFilterEditor
            && mFilterEditor->hasFocus()
            && (key == KEY_RETURN
                || key == KEY_DOWN)
            && mask == MASK_NONE)
        {
            // move focus to inventory proper
            mActivePanel->setFocus(true);
            root_folder->scrollToShowSelection();
            return true;
        }

        if (mActivePanel->hasFocus() && key == KEY_UP)
        {
            startSearch();
        }
        if(mSingleFolderMode && key == KEY_LEFT)
        {
            onBackFolderClicked();
        }
    }

    return LLPanel::handleKeyHere(key, mask);

}

//----------------------------------------------------------------------------
// menu callbacks

void LLPanelMainInventory::doToSelected(const LLSD& userdata)
{
    getPanel()->doToSelected(userdata);
}

void LLPanelMainInventory::closeAllFolders()
{
    getPanel()->getRootFolder()->closeAllFolders();
}

S32 get_instance_num()
{
    static S32 instance_num = 0;
    instance_num = (instance_num + 1) % S32_MAX;

    return instance_num;
}

LLFloaterSidePanelContainer* LLPanelMainInventory::newWindow()
{
    S32 instance_num = get_instance_num();

    if (!gAgentCamera.cameraMouselook())
    {
        LLFloaterSidePanelContainer* floater = LLFloaterReg::showTypedInstance<LLFloaterSidePanelContainer>("inventory", LLSD(instance_num));
        LLSidepanelInventory* sidepanel_inventory = floater->findChild<LLSidepanelInventory>("main_panel");
        sidepanel_inventory->initInventoryViews();
        return floater;
    }
    return NULL;
}

//static
void LLPanelMainInventory::newFolderWindow(LLUUID folder_id, LLUUID item_to_select)
{
    LLFloaterReg::const_instance_list_t& inst_list = LLFloaterReg::getFloaterList("inventory");
    for (LLFloaterReg::const_instance_list_t::const_iterator iter = inst_list.begin(); iter != inst_list.end();)
    {
        LLFloaterSidePanelContainer* inventory_container = dynamic_cast<LLFloaterSidePanelContainer*>(*iter++);
        if (inventory_container)
        {
            LLSidepanelInventory* sidepanel_inventory = dynamic_cast<LLSidepanelInventory*>(inventory_container->findChild<LLPanel>("main_panel", true));
            if (sidepanel_inventory)
            {
                LLPanelMainInventory* main_inventory = sidepanel_inventory->getMainInventoryPanel();
                if (main_inventory && main_inventory->isSingleFolderMode()
                    && (main_inventory->getCurrentSFVRoot() == folder_id))
                {
                    main_inventory->setFocus(true);
                    if(item_to_select.notNull())
                    {
                        main_inventory->setGallerySelection(item_to_select);
                    }
                    return;
                }
            }
        }
    }

    S32 instance_num = get_instance_num();

    LLFloaterSidePanelContainer* inventory_container = LLFloaterReg::showTypedInstance<LLFloaterSidePanelContainer>("inventory", LLSD(instance_num));
    if(inventory_container)
    {
        LLSidepanelInventory* sidepanel_inventory = dynamic_cast<LLSidepanelInventory*>(inventory_container->findChild<LLPanel>("main_panel", true));
        if (sidepanel_inventory)
        {
            LLPanelMainInventory* main_inventory = sidepanel_inventory->getMainInventoryPanel();
            if (main_inventory)
            {
                main_inventory->initSingleFolderRoot(folder_id);
                main_inventory->toggleViewMode();
                if(folder_id.notNull())
                {
                    if(item_to_select.notNull())
                    {
                        main_inventory->setGallerySelection(item_to_select, true);
                    }
                }
            }
        }
    }
}

void LLPanelMainInventory::doCreate(const LLSD& userdata)
{
    // <FS:Ansariel> FIRE-20108: Can't create new folder in secondary inventory if view is filtered
    //reset_inventory_filter();
    onFilterEdit("");
    // </FS:Ansariel>
    if(mSingleFolderMode)
    {
        if(isListViewMode() || isCombinationViewMode())
        {
            LLFolderViewItem* current_folder = getActivePanel()->getRootFolder();
            if (current_folder)
            {
                if(isCombinationViewMode())
                {
                    mForceShowInvLayout = true;
                }

                LLHandle<LLPanel> handle = getHandle();
                std::function<void(const LLUUID&)> callback_created = [handle](const LLUUID& new_id)
                {
                    gInventory.notifyObservers(); // not really needed, should have been already done
                    LLPanelMainInventory* panel = (LLPanelMainInventory*)handle.get();
                    if (new_id.notNull() && panel)
                    {
                        // might need to refresh visibility, delay rename
                        panel->mCombInvUUIDNeedsRename = new_id;

                        if (panel->isCombinationViewMode())
                        {
                            panel->mForceShowInvLayout = true;
                        }

                        LL_DEBUGS("Inventory") << "Done creating inventory: " << new_id << LL_ENDL;
                    }
                };
                menu_create_inventory_item(NULL, getCurrentSFVRoot(), userdata, LLUUID::null, callback_created);
            }
        }
        else
        {
            LLHandle<LLPanel> handle = getHandle();
            std::function<void(const LLUUID&)> callback_created = [handle](const LLUUID &new_id)
            {
                gInventory.notifyObservers(); // not really needed, should have been already done
                if (new_id.notNull())
                {
                    LLPanelMainInventory* panel = (LLPanelMainInventory*)handle.get();
                    if (panel)
                    {
                        panel->setGallerySelection(new_id);
                        LL_DEBUGS("Inventory") << "Done creating inventory: " << new_id << LL_ENDL;
                    }
                }
            };
            menu_create_inventory_item(NULL, getCurrentSFVRoot(), userdata, LLUUID::null, callback_created);
        }
    }
    else
    {
        menu_create_inventory_item(getPanel(), NULL, userdata);
    }
}

void LLPanelMainInventory::resetFilters()
{
    LLFloaterInventoryFinder *finder = getFinder();
    // <FS:Ansariel> Properly reset all filters
    //getCurrentFilter().resetDefault();
    LLInventoryFilter& filter = getCurrentFilter();
    filter.resetDefault();
    filter.setFilterCreator(LLInventoryFilter::FILTERCREATOR_ALL);
    filter.setSearchType(LLInventoryFilter::SEARCHTYPE_NAME);
    filter.setFilterPermissions(PERM_NONE); // <FS:Zi> FIRE-1175 - Filter Permissions Menu
    getActivePanel()->updateShowInboxFolder(gSavedSettings.getBOOL("FSShowInboxFolder"));
    getActivePanel()->updateHideEmptySystemFolders(gSavedSettings.getBOOL("DebugHideEmptySystemFolders"));
    updateFilterDropdown(&filter);
    // </FS:Ansariel>
    if (finder)
    {
        finder->updateElementsFromFilter();
    }

    setFilterTextFromFilter();
}

void LLPanelMainInventory::resetAllItemsFilters()
{
    LLFloaterInventoryFinder *finder = getFinder();
    getAllItemsPanel()->getFilter().resetDefault();
    if (finder)
    {
        finder->updateElementsFromFilter();
    }

    setFilterTextFromFilter();
}

void LLPanelMainInventory::findLinks(const LLUUID& item_id, const std::string& item_name)
{
    mFilterSubString = item_name;

    LLInventoryFilter &filter = mActivePanel->getFilter();
    filter.setFindAllLinksMode(item_name, item_id);

    mFilterEditor->setText(item_name);
    mFilterEditor->setFocus(true);
}

void LLPanelMainInventory::setSortBy(const LLSD& userdata)
{
    U32 sort_order_mask = getActivePanel()->getSortOrder();
    std::string sort_type = userdata.asString();
    if (sort_type == "name")
    {
        sort_order_mask &= ~LLInventoryFilter::SO_DATE;
    }
    else if (sort_type == "date")
    {
        sort_order_mask |= LLInventoryFilter::SO_DATE;
    }
    else if (sort_type == "foldersalwaysbyname")
    {
        if ( sort_order_mask & LLInventoryFilter::SO_FOLDERS_BY_NAME )
        {
            sort_order_mask &= ~LLInventoryFilter::SO_FOLDERS_BY_NAME;
        }
        else
        {
            sort_order_mask |= LLInventoryFilter::SO_FOLDERS_BY_NAME;
        }
    }
    else if (sort_type == "systemfolderstotop")
    {
        if ( sort_order_mask & LLInventoryFilter::SO_SYSTEM_FOLDERS_TO_TOP )
        {
            sort_order_mask &= ~LLInventoryFilter::SO_SYSTEM_FOLDERS_TO_TOP;
        }
        else
        {
            sort_order_mask |= LLInventoryFilter::SO_SYSTEM_FOLDERS_TO_TOP;
        }
    }
    if(mSingleFolderMode && !isListViewMode())
    {
        mCombinationGalleryPanel->setSortOrder(sort_order_mask, true);
    }

    getActivePanel()->setSortOrder(sort_order_mask);
    // <FS:Zi> Recent items panel should save sort order
    //if (isRecentItemsPanelSelected())
    //{
    //    gSavedSettings.setU32("RecentItemsSortOrder", sort_order_mask);
    //}
    //else
    //{
    //    gSavedSettings.setU32("InventorySortOrder", sort_order_mask);
    //}
    gSavedSettings.setU32(getActivePanel()->mSortOrderSetting, sort_order_mask);
    // </FS:Zi>
}

void LLPanelMainInventory::onSelectSearchType()
{
    std::string new_type = mSearchTypeCombo->getValue();
    if (new_type == "search_by_name")
    {
        setSearchType(LLInventoryFilter::SEARCHTYPE_NAME);
    }
    if (new_type == "search_by_creator")
    {
        setSearchType(LLInventoryFilter::SEARCHTYPE_CREATOR);
    }
    if (new_type == "search_by_description")
    {
        setSearchType(LLInventoryFilter::SEARCHTYPE_DESCRIPTION);
    }
    if (new_type == "search_by_UUID")
    {
        setSearchType(LLInventoryFilter::SEARCHTYPE_UUID);
    }
}

void LLPanelMainInventory::setSearchType(LLInventoryFilter::ESearchType type)
{
    if(mSingleFolderMode && isGalleryViewMode())
    {
        mCombinationGalleryPanel->setSearchType(type);
    }
    else if(mSingleFolderMode && isCombinationViewMode())
    {
        mCombinationInventoryPanel->setSearchType(type);
        mCombinationGalleryPanel->setSearchType(type);
    }
    else
    {
        getActivePanel()->setSearchType(type);
    }
}

void LLPanelMainInventory::updateSearchTypeCombo()
{
    // <FS:Ansariel> Check if combo box is actually there
    if (!mSearchTypeCombo)
    {
        return;
    }
    // </FS:Ansariel>

    LLInventoryFilter::ESearchType search_type(LLInventoryFilter::SEARCHTYPE_NAME);

    if(mSingleFolderMode && isGalleryViewMode())
    {
        search_type = mCombinationGalleryPanel->getSearchType();
    }
    else if(mSingleFolderMode && isCombinationViewMode())
    {
        search_type = mCombinationGalleryPanel->getSearchType();
    }
    else
    {
        search_type = getActivePanel()->getSearchType();
    }

    switch(search_type)
    {
        case LLInventoryFilter::SEARCHTYPE_CREATOR:
            mSearchTypeCombo->setValue("search_by_creator");
            break;
        case LLInventoryFilter::SEARCHTYPE_DESCRIPTION:
            mSearchTypeCombo->setValue("search_by_description");
            break;
        case LLInventoryFilter::SEARCHTYPE_UUID:
            mSearchTypeCombo->setValue("search_by_UUID");
            break;
        case LLInventoryFilter::SEARCHTYPE_NAME:
        default:
            mSearchTypeCombo->setValue("search_by_name");
            break;
    }
}

bool LLPanelMainInventory::isSortByChecked(const LLSD& userdata)
{
    U32 sort_order_mask = getActivePanel()->getSortOrder();
    const std::string command_name = userdata.asString();
    if (command_name == "name")
    {
        return !(sort_order_mask & LLInventoryFilter::SO_DATE);
    }

    if (command_name == "date")
    {
        return (sort_order_mask & LLInventoryFilter::SO_DATE);
    }

    if (command_name == "foldersalwaysbyname")
    {
        return (sort_order_mask & LLInventoryFilter::SO_FOLDERS_BY_NAME);
    }

    if (command_name == "systemfolderstotop")
    {
        return (sort_order_mask & LLInventoryFilter::SO_SYSTEM_FOLDERS_TO_TOP);
    }

    return false;
}
// </FS:Zi> Sort By menu handlers

// <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
// Callback method used to update the Show Filter button on the inventory bottom UI
bool LLPanelMainInventory::isAnyFilterChecked(const LLSD& userdata)
{
    // Validate that the command came from the right check box (Show Filters Modified)
    const std::string command_name = userdata.asString();
    if (command_name == "show_filters_modified")
    {
        // Only use thte active panel if it is valid
        if (mActivePanel)
        {
            // Get the current filter object
            LLInventoryFilter& filter = getCurrentFilter();
            // If either of the three filter checks are true, Is Not Default, Filter Creator Type is not set to all creators,
            // and Show Folder State is set to show folder state then we need to turn on the Show Filter Button check higlight,
            // so return true if any of these are true.
            return filter.isNotDefault() || filter.getFilterCreatorType() != LLInventoryFilter::FILTERCREATOR_ALL ||
                   filter.getShowFolderState() == LLInventoryFilter::SHOW_ALL_FOLDERS;
        }
    }

    return false;
}
// </FS:minerjr> [FIRE-35042]

// static
bool LLPanelMainInventory::filtersVisible(void* user_data)
{
    LLPanelMainInventory* self = (LLPanelMainInventory*)user_data;
    if(!self) return false;

    return self->getFinder() != NULL;
}

void LLPanelMainInventory::onClearSearch()
{
    bool initially_active = false;
    LLFloater *finder = getFinder();
    // <FS:Ansariel> Worn inventory panel
    //if (mActivePanel && (getActivePanel() != mWornItemsPanel))
    if (mActivePanel)
    // </FS:Ansariel>
    {
        // <FS:Ansariel> FIRE-5160: Don't reset inventory filter when clearing search term
        //initially_active = mActivePanel->getFilter().isNotDefault();
        //setFilterSubString(LLStringUtil::null);
        //mActivePanel->setFilterTypes(0xffffffffffffffffULL);
        //mActivePanel->setFilterLinks(LLInventoryFilter::FILTERLINK_INCLUDE_LINKS);
        setFilterSubString(LLStringUtil::null);
        // </FS:Ansariel>
    }

    if (finder)
    {
        LLFloaterInventoryFinder::selectAllTypes(finder);
    }

    // re-open folders that were initially open in case filter was active
    if (mActivePanel && (mFilterSubString.size() || initially_active) && !mSingleFolderMode)
    {
        mSavedFolderState->setApply(true);
        mActivePanel->getRootFolder()->applyFunctorRecursively(*mSavedFolderState);
        LLOpenFoldersWithSelection opener;
        mActivePanel->getRootFolder()->applyFunctorRecursively(opener);
        mActivePanel->getRootFolder()->scrollToShowSelection();
    }
    mFilterSubString = "";

    if (mInboxPanel)
    {
        mInboxPanel->onClearSearch();
    }
}

void LLPanelMainInventory::onFilterEdit(const std::string& search_string )
{
    if(mSingleFolderMode && isGalleryViewMode())
    {
        mFilterSubString = search_string;
        mCombinationGalleryPanel->setFilterSubString(mFilterSubString);
        return;
    }
    if(mSingleFolderMode && isCombinationViewMode())
    {
        mCombinationGalleryPanel->setFilterSubString(search_string);
    }

    if (search_string == "")
    {
        onClearSearch();
    }

    if (!mActivePanel)
    {
        return;
    }

    if (!LLInventoryModelBackgroundFetch::instance().inventoryFetchStarted())
    {
        llassert(false); // this should have been done on startup
        LLInventoryModelBackgroundFetch::instance().start();
    }

    mFilterSubString = search_string;
    // <FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
    //if (mActivePanel->getFilterSubString().empty() && mFilterSubString.empty())
    std::string search_for;
    if (gSavedSettings.getBOOL("FSSplitInventorySearchOverTabs"))
    {
        search_for = search_string;
    }
    else
    {
        search_for = mFilterSubString;
    }
    if (mActivePanel->getFilterSubString().empty() && search_for.empty())
    // </FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
    {
            // current filter and new filter empty, do nothing
            return;
    }

    // save current folder open state if no filter currently applied
    if (!mActivePanel->getFilter().isNotDefault())
    {
        mSavedFolderState->setApply(false);
        mActivePanel->getRootFolder()->applyFunctorRecursively(*mSavedFolderState);
    }

    // set new filter string
    // <FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
    //setFilterSubString(mFilterSubString);
    if (gSavedSettings.getBOOL("FSSplitInventorySearchOverTabs"))
    {
        setFilterSubString(search_string);
    }
    else
    {
        setFilterSubString(mFilterSubString);
    }
    // </FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)

    if (mInboxPanel)
    {
        mInboxPanel->onFilterEdit(search_string);
    }
}

// <FS:Zi> Filter dropdown
void LLPanelMainInventory::onFilterTypeSelected(const std::string& filter_type_name)
{
    if (!mActivePanel)
    {
        return;
    }

    // by default enable everything
    U64 filterTypes = ~0;

    // get the pointer to the filter subwindow
    LLFloaterInventoryFinder* finder = getFinder();

    // find the filter name in our filter map
    if (mFilterMap.find(filter_type_name) != mFilterMap.end())
    {
        filterTypes = mFilterMap[filter_type_name];
    }
    // special treatment for "all" filter
    else if (filter_type_name == "filter_type_all")
    {
        // update subwindow if it's open
        if (finder)
        {
            LLFloaterInventoryFinder::selectAllTypes(finder);
        }
    }
    // special treatment for "custom" filter
    else if (filter_type_name == "filter_type_custom")
    {
        // open the subwindow if needed, otherwise just give it focus
        if (!finder)
        {
            toggleFindOptions();
        }
        else
        {
            finder->setFocus(true);
        }

        return;
    }
    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible 
    // Special treatment for "coalesced" filter
    else if (filter_type_name == "filter_type_coalesced")
    {
        // Turn on Only Coalesced filter. This will also trigger the button "only_coalesced_inv_btn"
        // and menu item check "inventory_filter_coalesced_objects_only" toggle states.
        getActivePanel()->setFilterCoalescedObjects(true);
        // As all Coalesced objects are only objects, then set the filter type to filter_type_objects
        filterTypes = mFilterMap["filter_type_objects"];
    }
    // </FS:minerjr> [FIRE-35042]
    // invalid selection (broken XML?)
    else
    {
        LL_WARNS() << "Invalid filter selection: " << filter_type_name << LL_ENDL;
        return;
    }

    mActivePanel->setFilterTypes(filterTypes);
    // update subwindow if it's open
    if (finder)
    {
        finder->updateElementsFromFilter();
    }

    setFilterTextFromFilter();
}

// reflect state of current filter selection in the dropdown list
void LLPanelMainInventory::updateFilterDropdown(const LLInventoryFilter* filter)
{
    // if we don't have a filter combobox (missing in the skin and failed to create?) do nothing
    if (!mFilterComboBox)
    {
        return;
    }

    // extract filter bits we need to see
    U64 filterTypes = filter->getFilterObjectTypes() & mFilterMask;

    std::string controlName;

    // check if the filter types match our filter mask, meaning "All"
    if (filterTypes == mFilterMask)
    {
        controlName = "filter_type_all";
    }
    else
    {
        // find the name of the current filter in our filter map, if exists
        for (std::map<std::string, U64>::iterator it = mFilterMap.begin(); it != mFilterMap.end(); ++it)
        {
            if ((*it).second == filterTypes)
            {
                controlName = (*it).first;
                break;
            }
        }

        // no filter type found in the map, must be a custom filter
        if (controlName.empty())
        {
            controlName = "filter_type_custom";
        }
    }

    mFilterComboBox->setValue(controlName);
}
// </FS:Zi> Filter dropdown

 //static
 bool LLPanelMainInventory::incrementalFind(LLFolderViewItem* first_item, const char *find_text, bool backward)
 {
    LLPanelMainInventory* active_view = NULL;

    LLFloaterReg::const_instance_list_t& inst_list = LLFloaterReg::getFloaterList("inventory");
    for (LLFloaterReg::const_instance_list_t::const_iterator iter = inst_list.begin(); iter != inst_list.end(); ++iter)
    {
        LLPanelMainInventory* iv = dynamic_cast<LLPanelMainInventory*>(*iter);
        if (iv)
        {
            if (gFocusMgr.childHasKeyboardFocus(iv))
            {
                active_view = iv;
                break;
            }
        }
    }

    if (!active_view)
    {
        return false;
    }

    std::string search_string(find_text);

    if (search_string.empty())
    {
        return false;
    }

    if (active_view->getPanel() &&
        active_view->getPanel()->getRootFolder()->search(first_item, search_string, backward))
    {
        return true;
    }

    return false;
 }

void LLPanelMainInventory::onFilterSelected()
{
    // Find my index
    setActivePanel();

    if (!mActivePanel)
    {
        return;
    }

    // <FS:Ansariel> Worn inventory panel; We do this at init and only once for performance reasons!
    //if (getActivePanel() == mWornItemsPanel)
    //{
    //  mActivePanel->openAllFolders();
    //}
    // </FS:Ansariel>
    updateSearchTypeCombo();
    // <FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
    //setFilterSubString(mFilterSubString);
    if (!gSavedSettings.getBOOL("FSSplitInventorySearchOverTabs"))
    {
        setFilterSubString(mFilterSubString);
    }
    // </FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
    LLInventoryFilter& filter = getCurrentFilter();
    LLFloaterInventoryFinder *finder = getFinder();
    if (finder)
    {
        finder->changeFilter(&filter);
        if (mSingleFolderMode)
        {
            finder->setTitle(getLocalizedRootName());
        }
    }
    if (filter.isActive() && !LLInventoryModelBackgroundFetch::instance().inventoryFetchStarted())
    {
        llassert(false); // this should have been done on startup
        LLInventoryModelBackgroundFetch::instance().start();
    }
    updateFilterDropdown(&filter);  // <FS:Zi> Filter dropdown
    setFilterTextFromFilter();
}

const std::string LLPanelMainInventory::getFilterSubString()
{
    return mActivePanel->getFilterSubString();
}

void LLPanelMainInventory::setFilterSubString(const std::string& string)
{
    mActivePanel->setFilterSubString(string);
}

bool LLPanelMainInventory::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                                         EDragAndDropType cargo_type,
                                         void* cargo_data,
                                         EAcceptance* accept,
                                         std::string& tooltip_msg)
{
//  if (mFilterTabs)
//  {
//      // Check to see if we are auto scrolling from the last frame
//      LLInventoryPanel* panel = (LLInventoryPanel*)this->getActivePanel();
//      bool needsToScroll = panel->getScrollableContainer()->canAutoScroll(x, y);
//      if(needsToScroll)
//      {
//          mFilterTabs->startDragAndDropDelayTimer();
//      }
//  }
// [SL:KB] - Checked: UI-InvPanelDnD | Checked: 2011-06-16 (Catznip-2.6)
    LLInventoryPanel* pInvPanel = getActivePanel();
    if ( (pInvPanel) && (pInvPanel->pointInView(x - pInvPanel->getRect().mLeft, y - pInvPanel->getRect().mBottom)) )
    {
        pInvPanel->getScrollableContainer()->autoScroll(x, y);
    }
// [/SL:KB]

    bool handled = LLPanel::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);

    return handled;
}

// virtual
void LLPanelMainInventory::changed(U32)
{
    updateItemcountText();
}

void LLPanelMainInventory::setFocusOnFilterEditor()
{
    if (mFilterEditor)
    {
        mFilterEditor->setFocus(true);
    }
}

// virtual
void LLPanelMainInventory::draw()
{
    if (mActivePanel && mFilterEditor)
    {
        // <FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
        //mFilterEditor->setText(mFilterSubString);
        static LLCachedControl<bool> sfSplitInventorySearchOverTabs(gSavedSettings, "FSSplitInventorySearchOverTabs");
        if (sfSplitInventorySearchOverTabs)
        {
            mFilterEditor->setText(mActivePanel->getFilter().getFilterSubStringOrig());
        }
        else
        {
            mFilterEditor->setText(mFilterSubString);
        }
        // </FS:Ansariel> Separate search for inventory tabs from Satomi Ahn (FIRE-913 & FIRE-6862)
    }
    if (mActivePanel && mResortActivePanel)
    {
        // EXP-756: Force resorting of the list the first time we draw the list:
        // In the case of date sorting, we don't have enough information at initialization time
        // to correctly sort the folders. Later manual resort doesn't do anything as the order value is
        // set correctly. The workaround is to reset the order to alphabetical (or anything) then to the correct order.
        U32 order = mActivePanel->getSortOrder();
        mActivePanel->setSortOrder(LLInventoryFilter::SO_NAME);
        mActivePanel->setSortOrder(order);
        mResortActivePanel = false;
    }
    LLPanel::draw();
    updateItemcountText();
    updateCombinationVisibility();
}

void LLPanelMainInventory::updateItemcountText()
{
    bool update = false;
    if (mSingleFolderMode)
    {
        LLInventoryModel::cat_array_t* cats;
        LLInventoryModel::item_array_t* items;

        gInventory.getDirectDescendentsOf(getCurrentSFVRoot(), cats, items);
        S32 item_count = items ? (S32)items->size() : 0;
        S32 cat_count = cats ? (S32)cats->size() : 0;

        if (mItemCount != item_count)
        {
            mItemCount = item_count;
            update = true;
        }
        if (mCategoryCount != cat_count)
        {
            mCategoryCount = cat_count;
            update = true;
        }
    }
    else
    {
        if (mItemCount != gInventory.getItemCount())
        {
            mItemCount = gInventory.getItemCount();
            update = true;
        }

        if (mCategoryCount != gInventory.getCategoryCount())
        {
            mCategoryCount = gInventory.getCategoryCount();
            update = true;
        }

        EFetchState currentFetchState{ EFetchState::Unknown };
        if (LLInventoryModelBackgroundFetch::instance().folderFetchActive())
        {
            currentFetchState = EFetchState::Fetching;
        }
        else if (LLInventoryModelBackgroundFetch::instance().isEverythingFetched())
        {
            currentFetchState = EFetchState::Complete;
        }

        if (mLastFetchState != currentFetchState)
        {
            mLastFetchState = currentFetchState;
            update = true;
        }
    }

    if (mLastFilterText != getFilterText())
    {
        mLastFilterText = getFilterText();
        update = true;
    }

    if (update)
    {
        mItemCountString = "";
        // <FS:Ansariel> Include folders in inventory count
        //LLLocale locale(LLLocale::USER_LOCALE);
        //LLResMgr::getInstance()->getIntegerString(mItemCountString, mItemCount);

        //mCategoryCountString = "";
        //LLResMgr::getInstance()->getIntegerString(mCategoryCountString, mCategoryCount);
        LLLocale locale("");
        LLResMgr* resmgr = LLResMgr::getInstance();
        resmgr->getIntegerString(mItemCountString, mItemCount + mCategoryCount);
        // </FS:Ansariel>

        LLStringUtil::format_map_t string_args;
        string_args["[ITEM_COUNT]"] = mItemCountString;
        string_args["[CATEGORY_COUNT]"] = mCategoryCountString;
        string_args["[FILTER]"] = mLastFilterText;

        std::string text = "";

        if (mSingleFolderMode)
        {
            text = getString("ItemcountCompleted", string_args);
        }
        else
        {
            switch (mLastFetchState)
            {
            case EFetchState::Fetching:
                text = getString("ItemcountFetching", string_args);
                break;
            case EFetchState::Complete:
                text = getString("ItemcountCompleted", string_args);
                break;
            default:
                text = getString("ItemcountUnknown", string_args);
                break;
            }
        }

        mCounterCtrl->setValue(text);
        // <FS:Ansariel> Include folders in inventory count
        //mCounterCtrl->setToolTip(text);
        std::string item_str, category_str;
        resmgr->getIntegerString(item_str, mItemCount);
        resmgr->getIntegerString(category_str, mCategoryCount);
        LLStringUtil::format_map_t args;
        args["[ITEMS]"] = item_str;
        args["[CATEGORIES]"] = category_str;
        mCounterCtrl->setToolTipArgs(args);
        // </FS:Ansariel>
    }
}

void LLPanelMainInventory::onFocusReceived()
{
    LLSidepanelInventory *sidepanel_inventory = LLFloaterSidePanelContainer::getPanel<LLSidepanelInventory>("inventory");
    if (!sidepanel_inventory)
    {
        LL_WARNS() << "Could not find Inventory Panel in My Inventory floater" << LL_ENDL;
        return;
    }

    sidepanel_inventory->clearSelections(false, true);
}

void LLPanelMainInventory::setFilterTextFromFilter()
{
    // <FS:Zi> Filter dropdown
    //mFilterText = getCurrentFilter().getFilterText();
    // this method gets called by the filter subwindow (once every frame), so we update our combo box here
    LLInventoryFilter &filter = getCurrentFilter();
    updateFilterDropdown(&filter);
    mFilterText = filter.getFilterText();
    // </FS:Zi> Filter dropdown
}

void LLPanelMainInventory::toggleFindOptions()
{
    LLFloater *floater = getFinder();
    if (!floater)
    {
        LLFloaterInventoryFinder * finder = new LLFloaterInventoryFinder(this);
        mFinderHandle = finder->getHandle();
        finder->openFloater();

        LLFloater* parent_floater = gFloaterView->getParentFloater(this);
        if (parent_floater)
            parent_floater->addDependentFloater(mFinderHandle);

        if (!LLInventoryModelBackgroundFetch::instance().inventoryFetchStarted())
        {
            llassert(false); // this should have been done on startup
            LLInventoryModelBackgroundFetch::instance().start();
        }

        if (mSingleFolderMode)
        {
            finder->setTitle(getLocalizedRootName());
        }
    }
    else
    {
        floater->closeFloater();
    }
}

void LLPanelMainInventory::setSelectCallback(const LLFolderView::signal_t::slot_type& cb)
{
    mAllItemsPanel->setSelectCallback(cb);
    mRecentPanel->setSelectCallback(cb);
    getChild<LLInventoryPanel>("Worn Items")->setSelectCallback(cb);
}

void LLPanelMainInventory::onSelectionChange(LLInventoryPanel *panel, const std::deque<LLFolderViewItem*>& items, bool user_action)
{
    updateListCommands();
    panel->onSelectionChange(items, user_action);
}

///----------------------------------------------------------------------------
/// LLFloaterInventoryFinder
///----------------------------------------------------------------------------

LLFloaterInventoryFinder* LLPanelMainInventory::getFinder()
{
    return (LLFloaterInventoryFinder*)mFinderHandle.get();
}


LLFloaterInventoryFinder::LLFloaterInventoryFinder(LLPanelMainInventory* inventory_view) :
    LLFloater(LLSD()),
    mPanelMainInventory(inventory_view),
    mFilter(&inventory_view->getPanel()->getFilter())
{
    buildFromFile("floater_inventory_view_finder.xml");
    updateElementsFromFilter();
}

bool LLFloaterInventoryFinder::postBuild()
{
    const LLRect& viewrect = mPanelMainInventory->getRect();
    setRect(LLRect(viewrect.mLeft - getRect().getWidth(), viewrect.mTop, viewrect.mLeft, viewrect.mTop - getRect().getHeight()));

    childSetAction("All", selectAllTypes, this);
    childSetAction("None", selectNoTypes, this);

    mSpinSinceHours = getChild<LLSpinCtrl>("spin_hours_ago");
    childSetCommitCallback("spin_hours_ago", onTimeAgo, this);

    mSpinSinceDays = getChild<LLSpinCtrl>("spin_days_ago");
    childSetCommitCallback("spin_days_ago", onTimeAgo, this);

    mCreatorSelf = getChild<LLCheckBoxCtrl>("check_created_by_me");
    mCreatorOthers = getChild<LLCheckBoxCtrl>("check_created_by_others");
    mCreatorSelf->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onCreatorSelfFilterCommit, this));
    mCreatorOthers->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onCreatorOtherFilterCommit, this));

    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    // Get the Finder's Only Coalesced check box for use later on, instead of calling getChild everytime accessing it
    mOnlyCoalescedFilterCheck = getChild<LLCheckBoxCtrl>("check_only_coalesced");
    // If the checkbox could be found, then set the commit callback to onOnlyCoalescedFilterCommit to update the mFilter object
    // with the value in the checkbox.
    if (mOnlyCoalescedFilterCheck)
    {
        mOnlyCoalescedFilterCheck->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onOnlyCoalescedFilterCommit, this));
    }
    // Get the Finder's Show Links Filter combobox for use later as well to also prevent having to call get child everytime accessing it.
    mShowLinksFilterCombo = getChild<LLComboBox>("inventory_filter_show_links_combo");
    // If the combobox could be found, then set the commit callback to onShowLinksFilterCommit to update the mFilter object
    // with the value in the checkbox.
    if (mShowLinksFilterCombo)
    {
        mShowLinksFilterCombo->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onShowLinksFilterCommit, this));
    }
    // </FS:minerjr> [FIRE-35042]

    childSetAction("Close", onCloseBtn, this);

    // <FS:Ansariel> FIRE-5160: Don't reset inventory filter when clearing search term
    getChild<LLButton>("btnReset")->setClickedCallback(boost::bind(&LLFloaterInventoryFinder::onResetBtn, this));

    updateElementsFromFilter();

    // <FS:Zi> FIRE-1175 - Filter Permissions Menu
    getChild<LLUICtrl>("check_modify")->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onPermissionsChanged, this));
    getChild<LLUICtrl>("check_copy")->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onPermissionsChanged, this));
    getChild<LLUICtrl>("check_transfer")->setCommitCallback(boost::bind(&LLFloaterInventoryFinder::onPermissionsChanged, this));
    // </FS:Zi>

    return true;
}
void LLFloaterInventoryFinder::onTimeAgo(LLUICtrl *ctrl, void *user_data)
{
    LLFloaterInventoryFinder *self = (LLFloaterInventoryFinder *)user_data;
    if (!self) return;

    if ( self->mSpinSinceDays->get() ||  self->mSpinSinceHours->get() )
    {
        self->getChild<LLUICtrl>("check_since_logoff")->setValue(false);

        U32 days = (U32)self->mSpinSinceDays->get();
        U32 hours = (U32)self->mSpinSinceHours->get();
        if (hours >= 24)
        {
            // Try to handle both cases of spinner clicking and text input in a sensible fashion as best as possible.
            // There is no way to tell if someone has clicked the spinner to get to 24 or input 24 manually, so in
            // this case add to days.  Any value > 24 means they have input the hours manually, so do not add to the
            // current day value.
            if (24 == hours)  // Got to 24 via spinner clicking or text input of 24
            {
                days = days + hours / 24;
            }
            else    // Text input, so do not add to days
            {
                days = hours / 24;
            }
            hours = (U32)hours % 24;
            self->mSpinSinceHours->setFocus(false);
            self->mSpinSinceDays->setFocus(false);
            self->mSpinSinceDays->set((F32)days);
            self->mSpinSinceHours->set((F32)hours);
            self->mSpinSinceHours->setFocus(true);
        }
    }
}

// <FS:Ansariel> FIRE-5160: Don't reset inventory filter when clearing search term
void LLFloaterInventoryFinder::onResetBtn()
{
    mPanelMainInventory->resetFilters();
}
// </FS:Ansariel>

void LLFloaterInventoryFinder::changeFilter(LLInventoryFilter* filter)
{
    mFilter = filter;
    updateElementsFromFilter();
}

void LLFloaterInventoryFinder::updateElementsFromFilter()
{
    if (!mFilter)
        return;

    // Get data needed for filter display
    U32 filter_types = (U32)mFilter->getFilterObjectTypes();
    LLInventoryFilter::EFolderShow show_folders = mFilter->getShowFolderState();
    U32 hours = mFilter->getHoursAgo();
    U32 date_search_direction = mFilter->getDateSearchDirection();

    LLInventoryFilter::EFilterCreatorType filter_creator = mFilter->getFilterCreatorType();
    bool show_created_by_me = ((filter_creator == LLInventoryFilter::FILTERCREATOR_ALL) || (filter_creator == LLInventoryFilter::FILTERCREATOR_SELF));
    bool show_created_by_others = ((filter_creator == LLInventoryFilter::FILTERCREATOR_ALL) || (filter_creator == LLInventoryFilter::FILTERCREATOR_OTHERS));

    // update the ui elements
    // <FS:PP> Make floater title translatable
    // setTitle(mFilter->getName());
    setTitle(LLTrans::getString(mFilter->getName()));
    // </FS:PP>

    getChild<LLUICtrl>("check_animation")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_ANIMATION));

    getChild<LLUICtrl>("check_calling_card")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_CALLINGCARD));
    getChild<LLUICtrl>("check_clothing")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_WEARABLE));
    getChild<LLUICtrl>("check_gesture")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_GESTURE));
    getChild<LLUICtrl>("check_landmark")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_LANDMARK));
    getChild<LLUICtrl>("check_material")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_MATERIAL));
    getChild<LLUICtrl>("check_notecard")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_NOTECARD));
    getChild<LLUICtrl>("check_object")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_OBJECT));
    getChild<LLUICtrl>("check_script")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_LSL));
    getChild<LLUICtrl>("check_sound")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_SOUND));
    getChild<LLUICtrl>("check_texture")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_TEXTURE));
    getChild<LLUICtrl>("check_snapshot")->setValue((S32) (filter_types & 0x1 << LLInventoryType::IT_SNAPSHOT));
    getChild<LLUICtrl>("check_settings")->setValue((S32)(filter_types & 0x1 << LLInventoryType::IT_SETTINGS));
    getChild<LLUICtrl>("check_show_empty")->setValue(show_folders == LLInventoryFilter::SHOW_ALL_FOLDERS);

    getChild<LLUICtrl>("check_created_by_me")->setValue(show_created_by_me);
    getChild<LLUICtrl>("check_created_by_others")->setValue(show_created_by_others);

    getChild<LLUICtrl>("check_since_logoff")->setValue(mFilter->isSinceLogoff());
    mSpinSinceHours->set((F32)(hours % 24));
    mSpinSinceDays->set((F32)(hours / 24));
    getChild<LLRadioGroup>("date_search_direction")->setSelectedIndex(date_search_direction);

    // <FS:Zi> FIRE-1175 - Filter Permissions Menu
    getChild<LLUICtrl>("check_modify")->setValue((bool) (mFilter->getFilterPermissions() & PERM_MODIFY));
    getChild<LLUICtrl>("check_copy")->setValue((bool) (mFilter->getFilterPermissions() & PERM_COPY));
    getChild<LLUICtrl>("check_transfer")->setValue((bool) (mFilter->getFilterPermissions() & PERM_TRANSFER));
    // </FS:Zi>

    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    // Sync the Only Coalesced Filter Checkbox with the value from the mFilter
    // Make sure the Only Coalseced Filter checkbox and the filter are valid before accessing them.
    if (mOnlyCoalescedFilterCheck && mFilter)
    {
        // Set the check value to the value of the UI to the Only Coalesced Objects of the mFilter
        mOnlyCoalescedFilterCheck->set(mFilter->getFilterCoalescedObjects());
    }
    // Sync the Show Links Filter combo box with the value from the mFilter
    // Make sure the Show Links Filter combo box and filter are both valid
    if (mShowLinksFilterCombo && mFilter)
    {
        // Set the combo box value to the value of the FitlerLinks of the mFilter
        // In the UI, the choices match the same values as the filter values
        // 0 - Show Links, 2 Show Links Only, 1 = Hide Links
        // So we convert from the filters from U64 to LLSD (integer) as the SelectByValue takes a LLSD object as an input
        mShowLinksFilterCombo->selectByValue(LLSD(mFilter->getFilterLinks()));
    }
    // </FS:minerjr> [FIRE-35042]
}

void LLFloaterInventoryFinder::draw()
{
    U64 filter = 0xffffffffffffffffULL;
    bool filtered_by_all_types = true;

    if (!getChild<LLUICtrl>("check_animation")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_ANIMATION);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_calling_card")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_CALLINGCARD);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_clothing")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_WEARABLE);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_gesture")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_GESTURE);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_landmark")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_LANDMARK);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_material")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_MATERIAL);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_notecard")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_NOTECARD);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_object")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_OBJECT);
        filter &= ~(0x1 << LLInventoryType::IT_ATTACHMENT);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_script")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_LSL);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_sound")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_SOUND);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_texture")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_TEXTURE);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_snapshot")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_SNAPSHOT);
        filtered_by_all_types = false;
    }

    if (!getChild<LLUICtrl>("check_settings")->getValue())
    {
        filter &= ~(0x1 << LLInventoryType::IT_SETTINGS);
        filtered_by_all_types = false;
    }

    if (!filtered_by_all_types || (mPanelMainInventory->getPanel()->getFilter().getFilterTypes() & LLInventoryFilter::FILTERTYPE_DATE))
    {
        // don't include folders in filter, unless I've selected everything or filtering by date
        filter &= ~(0x1 << LLInventoryType::IT_CATEGORY);
    }

    bool is_sf_mode = mPanelMainInventory->isSingleFolderMode();
    if (is_sf_mode && mPanelMainInventory->isGalleryViewMode())
    {
        mPanelMainInventory->mCombinationGalleryPanel->getFilter().setShowFolderState(getCheckShowEmpty() ?
            LLInventoryFilter::SHOW_ALL_FOLDERS : LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
        mPanelMainInventory->mCombinationGalleryPanel->getFilter().setFilterObjectTypes(filter);
    }
    else
    {
        if (is_sf_mode && mPanelMainInventory->isCombinationViewMode())
        {
            mPanelMainInventory->mCombinationGalleryPanel->getFilter().setShowFolderState(getCheckShowEmpty() ?
                LLInventoryFilter::SHOW_ALL_FOLDERS : LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
            mPanelMainInventory->mCombinationGalleryPanel->getFilter().setFilterObjectTypes(filter);
        }
        // update the panel, panel will update the filter
        mPanelMainInventory->getPanel()->setShowFolderState(getCheckShowEmpty() ?
            LLInventoryFilter::SHOW_ALL_FOLDERS : LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
        mPanelMainInventory->getPanel()->setFilterTypes(filter);
    }

    if (getCheckSinceLogoff())
    {
        mSpinSinceDays->set(0);
        mSpinSinceHours->set(0);
    }
    U32 days = (U32)mSpinSinceDays->get();
    U32 hours = (U32)mSpinSinceHours->get();
    if (hours >= 24)
    {
        days = hours / 24;
        hours = (U32)hours % 24;
        // A UI element that has focus will not display a new value set to it
        mSpinSinceHours->setFocus(false);
        mSpinSinceDays->setFocus(false);
        mSpinSinceDays->set((F32)days);
        mSpinSinceHours->set((F32)hours);
        mSpinSinceHours->setFocus(true);
    }
    hours += days * 24;

    mPanelMainInventory->setFilterTextFromFilter();
    if (is_sf_mode && mPanelMainInventory->isGalleryViewMode())
    {
        mPanelMainInventory->mCombinationGalleryPanel->getFilter().setHoursAgo(hours);
        mPanelMainInventory->mCombinationGalleryPanel->getFilter().setDateRangeLastLogoff(getCheckSinceLogoff());
        mPanelMainInventory->mCombinationGalleryPanel->getFilter().setDateSearchDirection(getDateSearchDirection());
    }
    else
    {
        if (is_sf_mode && mPanelMainInventory->isCombinationViewMode())
        {
            mPanelMainInventory->mCombinationGalleryPanel->getFilter().setHoursAgo(hours);
            mPanelMainInventory->mCombinationGalleryPanel->getFilter().setDateRangeLastLogoff(getCheckSinceLogoff());
            mPanelMainInventory->mCombinationGalleryPanel->getFilter().setDateSearchDirection(getDateSearchDirection());
        }
        mPanelMainInventory->getPanel()->setHoursAgo(hours);
        mPanelMainInventory->getPanel()->setSinceLogoff(getCheckSinceLogoff());
        mPanelMainInventory->getPanel()->setDateSearchDirection(getDateSearchDirection());
    }

    LLPanel::draw();
}

void LLFloaterInventoryFinder::onCreatorSelfFilterCommit()
{
    bool show_creator_self = mCreatorSelf->getValue();
    bool show_creator_other = mCreatorOthers->getValue();

    if(show_creator_self && show_creator_other)
    {
        mPanelMainInventory->getCurrentFilter().setFilterCreator(LLInventoryFilter::FILTERCREATOR_ALL);
    }
    else if(show_creator_self)
    {
        mPanelMainInventory->getCurrentFilter().setFilterCreator(LLInventoryFilter::FILTERCREATOR_SELF);
    }
    else if(!show_creator_self || !show_creator_other)
    {
        mPanelMainInventory->getCurrentFilter().setFilterCreator(LLInventoryFilter::FILTERCREATOR_OTHERS);
        mCreatorOthers->set(true);
    }
}

void LLFloaterInventoryFinder::onCreatorOtherFilterCommit()
{
    bool show_creator_self = mCreatorSelf->getValue();
    bool show_creator_other = mCreatorOthers->getValue();

    if(show_creator_self && show_creator_other)
    {
        mPanelMainInventory->getCurrentFilter().setFilterCreator(LLInventoryFilter::FILTERCREATOR_ALL);
    }
    else if(show_creator_other)
    {
        mPanelMainInventory->getCurrentFilter().setFilterCreator(LLInventoryFilter::FILTERCREATOR_OTHERS);
    }
    else if(!show_creator_other || !show_creator_self)
    {
        mPanelMainInventory->getCurrentFilter().setFilterCreator(LLInventoryFilter::FILTERCREATOR_SELF);
        mCreatorSelf->set(true);
    }
}

// <FS:Zi> FIRE-1175 - Filter Permissions Menu
void LLFloaterInventoryFinder::onPermissionsChanged()
{
    PermissionMask perms = PERM_NONE;

    if (getChild<LLUICtrl>("check_modify")->getValue().asBoolean())
    {
        perms |= PERM_MODIFY;
    }

    if (getChild<LLUICtrl>("check_copy")->getValue().asBoolean())
    {
        perms |= PERM_COPY;
    }

    if (getChild<LLUICtrl>("check_transfer")->getValue().asBoolean())
    {
        perms |= PERM_TRANSFER;
    }

    mFilter->setFilterPermissions(perms);
}
// </FS:Zi>

// <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
// Callback method used to update the mFilter's Only Coalesced filter and syncs with the main inventory filter
void LLFloaterInventoryFinder::onOnlyCoalescedFilterCommit()
{
    // This will sync the Filter panels value with the value of the mFilter object
    if (mOnlyCoalescedFilterCheck && mFilter)
    {
        // Set the mFilter's Filter Coalesced Objects value to the Only Coalesced Filter Checkbox value
        mFilter->setFilterCoalescedObjects(mOnlyCoalescedFilterCheck->getValue());        
    }
}
// Callback method used to update the mFilter's Show Links filter and syncs with the main inventory filter
void LLFloaterInventoryFinder::onShowLinksFilterCommit()
{
    // This will sync the Show Links combo box with the value of the main inventory filter
    if (mShowLinksFilterCombo)
    {
        // Set the mFilter's Filter Links value to the selected value of the Show Links Filter Combo.
        // The values match up to the bit values that are used by the filter (0 = Show Links, 1 = Show Links Only, 2 = Hide Links)
        mFilter->setFilterLinks((U64)mShowLinksFilterCombo->getSelectedValue().asInteger());
    }
}
// </FS:minerjr> [FIRE-35042]

bool LLFloaterInventoryFinder::getCheckShowEmpty()
{
    return getChild<LLUICtrl>("check_show_empty")->getValue();
}

bool LLFloaterInventoryFinder::getCheckSinceLogoff()
{
    return getChild<LLUICtrl>("check_since_logoff")->getValue();
}

U32 LLFloaterInventoryFinder::getDateSearchDirection()
{
    return  getChild<LLRadioGroup>("date_search_direction")->getSelectedIndex();
}

void LLFloaterInventoryFinder::onCloseBtn(void* user_data)
{
    LLFloaterInventoryFinder* finderp = (LLFloaterInventoryFinder*)user_data;
    finderp->closeFloater();
}

// static
void LLFloaterInventoryFinder::selectAllTypes(void* user_data)
{
    LLFloaterInventoryFinder* self = (LLFloaterInventoryFinder*)user_data;
    if(!self) return;

    self->getChild<LLUICtrl>("check_animation")->setValue(true);
    self->getChild<LLUICtrl>("check_calling_card")->setValue(true);
    self->getChild<LLUICtrl>("check_clothing")->setValue(true);
    self->getChild<LLUICtrl>("check_gesture")->setValue(true);
    self->getChild<LLUICtrl>("check_landmark")->setValue(true);
    self->getChild<LLUICtrl>("check_material")->setValue(true);
    self->getChild<LLUICtrl>("check_notecard")->setValue(true);
    self->getChild<LLUICtrl>("check_object")->setValue(true);
    self->getChild<LLUICtrl>("check_script")->setValue(true);
    self->getChild<LLUICtrl>("check_sound")->setValue(true);
    self->getChild<LLUICtrl>("check_texture")->setValue(true);
    self->getChild<LLUICtrl>("check_snapshot")->setValue(true);
    self->getChild<LLUICtrl>("check_settings")->setValue(true);
}

//static
void LLFloaterInventoryFinder::selectNoTypes(void* user_data)
{
    LLFloaterInventoryFinder* self = (LLFloaterInventoryFinder*)user_data;
    if(!self) return;

    self->getChild<LLUICtrl>("check_animation")->setValue(false);
    self->getChild<LLUICtrl>("check_calling_card")->setValue(false);
    self->getChild<LLUICtrl>("check_clothing")->setValue(false);
    self->getChild<LLUICtrl>("check_gesture")->setValue(false);
    self->getChild<LLUICtrl>("check_landmark")->setValue(false);
    self->getChild<LLUICtrl>("check_material")->setValue(false);
    self->getChild<LLUICtrl>("check_notecard")->setValue(false);
    self->getChild<LLUICtrl>("check_object")->setValue(false);
    self->getChild<LLUICtrl>("check_script")->setValue(false);
    self->getChild<LLUICtrl>("check_sound")->setValue(false);
    self->getChild<LLUICtrl>("check_texture")->setValue(false);
    self->getChild<LLUICtrl>("check_snapshot")->setValue(false);
    self->getChild<LLUICtrl>("check_settings")->setValue(false);
}

// <FS:Zi> Inventory Collapse and Expand Buttons
void LLPanelMainInventory::onCollapseButtonClicked()
{
//  mFilterEditor->clear();
    onFilterEdit("");
    getPanel()->closeAllFolders();
}

void LLPanelMainInventory::onExpandButtonClicked()
{
    getPanel()->openAllFolders();
}
// </FS:Zi> Inventory Collapse and Expand Buttons

// <FS:Ansariel> FIRE-19493: "Show Original" should open main inventory panel
void LLPanelMainInventory::showAllItemsPanel()
{
    mFilterTabs->selectTabByName("All Items");
}
// </FS:Ansariel>

//////////////////////////////////////////////////////////////////////////////////
// List Commands                                                                //

void LLPanelMainInventory::initListCommandsHandlers()
{
    childSetAction("trash_btn", boost::bind(&LLPanelMainInventory::onTrashButtonClick, this)); // <FS:Ansariel> Keep better inventory layout
    childSetAction("add_btn", boost::bind(&LLPanelMainInventory::onAddButtonClick, this));
    mViewModeBtn->setCommitCallback(boost::bind(&LLPanelMainInventory::onViewModeClick, this));
    mUpBtn->setCommitCallback(boost::bind(&LLPanelMainInventory::onUpFolderClicked, this));
    mBackBtn->setCommitCallback(boost::bind(&LLPanelMainInventory::onBackFolderClicked, this));
    mForwardBtn->setCommitCallback(boost::bind(&LLPanelMainInventory::onForwardFolderClicked, this));

    // <FS:Ansariel> Keep better inventory layout
    mTrashButton = getChild<LLDragAndDropButton>("trash_btn");
    mTrashButton->setDragAndDropHandler(boost::bind(&LLPanelMainInventory::handleDragAndDropToTrash, this
            ,   _4 // bool drop
            ,   _5 // EDragAndDropType cargo_type
            ,   _7 // EAcceptance* accept
            ));
    // </FS:Ansariel>

    // <FS:Ansariel> Moved to constructor to register early
    //mCommitCallbackRegistrar.add("Inventory.GearDefault.Custom.Action", boost::bind(&LLPanelMainInventory::onCustomAction, this, _2));
    //mEnableCallbackRegistrar.add("Inventory.GearDefault.Check", boost::bind(&LLPanelMainInventory::isActionChecked, this, _2));
    //mEnableCallbackRegistrar.add("Inventory.GearDefault.Enable", boost::bind(&LLPanelMainInventory::isActionEnabled, this, _2));
    //mEnableCallbackRegistrar.add("Inventory.GearDefault.Visible", boost::bind(&LLPanelMainInventory::isActionVisible, this, _2));
    // </FS:Ansariel>
    mMenuGearDefault = LLUICtrlFactory::getInstance()->createFromFile<LLToggleableMenu>("menu_inventory_gear_default.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    mGearMenuButton->setMenu(mMenuGearDefault, LLMenuButton::MP_BOTTOM_LEFT, true);
    mMenuViewDefault = LLUICtrlFactory::getInstance()->createFromFile<LLToggleableMenu>("menu_inventory_view_default.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (mViewMenuButton) // <FS:Ansariel> Keep better inventory layout
        mViewMenuButton->setMenu(mMenuViewDefault, LLMenuButton::MP_BOTTOM_LEFT, true);
    LLMenuGL* menu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>("menu_inventory_add.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    mMenuAddHandle = menu->getHandle();

    mMenuVisibility = LLUICtrlFactory::getInstance()->createFromFile<LLToggleableMenu>("menu_inventory_search_visibility.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    mVisibilityMenuButton->setMenu(mMenuVisibility, LLMenuButton::MP_BOTTOM_LEFT, true);

    // Update the trash button when selected item(s) get worn or taken off.
    LLOutfitObserver::instance().addCOFChangedCallback(boost::bind(&LLPanelMainInventory::updateListCommands, this));
}

void LLPanelMainInventory::updateListCommands()
{
    // <FS:Ansariel> Keep better inventory layout
    bool trash_enabled = isActionEnabled("delete");
    mTrashButton->setEnabled(trash_enabled);
    // </FS:Ansariel>
}

void LLPanelMainInventory::onAddButtonClick()
{
// Gray out the "New Folder" option when the Recent tab is active as new folders will not be displayed
// unless "Always show folders" is checked in the filter options.

    LLMenuGL* menu = (LLMenuGL*)mMenuAddHandle.get();
    if (menu)
    {
        disableAddIfNeeded();

        setUploadCostIfNeeded();

        showActionMenu(menu,"add_btn");
    }
}

void LLPanelMainInventory::setActivePanel()
{
    // Todo: should cover gallery mode in some way
    if(mSingleFolderMode && (isListViewMode() || isCombinationViewMode()))
    {
        mActivePanel = mCombinationInventoryPanel;
    }
    else
    {
        mActivePanel = (LLInventoryPanel*)mFilterTabs->getCurrentPanel();
    }
    mViewModeBtn->setEnabled(mSingleFolderMode || (getAllItemsPanel() == getActivePanel()));
}

void LLPanelMainInventory::initSingleFolderRoot(const LLUUID& start_folder_id)
{
    mCombinationInventoryPanel->initFolderRoot(start_folder_id);
}

void LLPanelMainInventory::initInventoryViews()
{
    mAllItemsPanel->initializeViewBuilding();
    mRecentPanel->initializeViewBuilding();
    mWornItemsPanel->initializeViewBuilding();
}

void LLPanelMainInventory::toggleViewMode()
{
    if(mSingleFolderMode && isCombinationViewMode() && mCombinationGalleryPanel->getRootFolder().notNull())
    {
        mCombinationInventoryPanel->getRootFolder()->setForceArrange(false);
    }

    mSingleFolderMode = !mSingleFolderMode;
    mReshapeInvLayout = true;

    if (mCombinationGalleryPanel->getRootFolder().isNull())
    {
        mCombinationGalleryPanel->setRootFolder(mCombinationInventoryPanel->getSingleFolderRoot());
        mCombinationGalleryPanel->updateRootFolder();
    }

    updatePanelVisibility();
    // <FS:Ansariel> Disable Expand/Collapse buttons in single folder mode
    getChild<LLLayoutPanel>("collapse_expand_buttons")->setVisible(!mSingleFolderMode);

    setActivePanel();
    updateTitle();
    onFilterSelected();

    if (mParentSidepanel)
    {
        if(mSingleFolderMode)
        {
            mParentSidepanel->hideInbox();
        }
        else
        {
            mParentSidepanel->toggleInbox();
        }
    }
}

void LLPanelMainInventory::onViewModeClick()
{
    LLUUID selected_folder;
    LLUUID new_root_folder;
    if(mSingleFolderMode)
    {
        selected_folder = getCurrentSFVRoot();
    }
    else
    {
        LLFolderView* root = getActivePanel()->getRootFolder();
        std::set<LLFolderViewItem*> selection_set = root->getSelectionList();
        if (selection_set.size() == 1)
        {
            LLFolderViewItem* current_item = *selection_set.begin();
            if (current_item)
            {
                const LLUUID& id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
                if(gInventory.getCategory(id) != NULL)
                {
                    new_root_folder = id;
                }
                else
                {
                    const LLViewerInventoryItem* selected_item = gInventory.getItem(id);
                    if (selected_item && selected_item->getParentUUID().notNull())
                    {
                        new_root_folder = selected_item->getParentUUID();
                        selected_folder = id;
                    }
                }
            }
        }
        mCombinationInventoryPanel->initFolderRoot(new_root_folder);
    }

    toggleViewMode();

    if (mSingleFolderMode && new_root_folder.notNull())
    {
        setSingleFolderViewRoot(new_root_folder, true);
        if(selected_folder.notNull() && isListViewMode())
        {
            getActivePanel()->setSelection(selected_folder, TAKE_FOCUS_YES);
        }
    }
    else
    {
        if(selected_folder.notNull())
        {
            selectAllItemsPanel();
            getActivePanel()->setSelection(selected_folder, TAKE_FOCUS_YES);
        }
    }
}

void LLPanelMainInventory::onUpFolderClicked()
{
    const LLViewerInventoryCategory* cat = gInventory.getCategory(getCurrentSFVRoot());
    if (cat)
    {
        if (cat->getParentUUID().notNull())
        {
            if(isListViewMode())
            {
                mCombinationInventoryPanel->changeFolderRoot(cat->getParentUUID());
            }
            if(isGalleryViewMode())
            {
                mCombinationGalleryPanel->setRootFolder(cat->getParentUUID());
            }
            if(isCombinationViewMode())
            {
                mCombinationInventoryPanel->changeFolderRoot(cat->getParentUUID());
            }
        }
    }
}

void LLPanelMainInventory::onBackFolderClicked()
{
    if(isListViewMode())
    {
        mCombinationInventoryPanel->onBackwardFolder();
    }
    if(isGalleryViewMode())
    {
        mCombinationGalleryPanel->onBackwardFolder();
    }
    if(isCombinationViewMode())
    {
        mCombinationInventoryPanel->onBackwardFolder();
    }
}

void LLPanelMainInventory::onForwardFolderClicked()
{
    if(isListViewMode())
    {
        mCombinationInventoryPanel->onForwardFolder();
    }
    if(isGalleryViewMode())
    {
        mCombinationGalleryPanel->onForwardFolder();
    }
    if(isCombinationViewMode())
    {
        mCombinationInventoryPanel->onForwardFolder();
    }
}

void LLPanelMainInventory::setSingleFolderViewRoot(const LLUUID& folder_id, bool clear_nav_history)
{
    if(isListViewMode())
    {
        mCombinationInventoryPanel->changeFolderRoot(folder_id);
        if(clear_nav_history)
        {
            mCombinationInventoryPanel->clearNavigationHistory();
        }
    }
    else if(isGalleryViewMode())
    {
        mCombinationGalleryPanel->setRootFolder(folder_id);
        if(clear_nav_history)
        {
            mCombinationGalleryPanel->clearNavigationHistory();
        }
    }
    else if(isCombinationViewMode())
    {
        mCombinationInventoryPanel->changeFolderRoot(folder_id);
    }
    updateNavButtons();
}

LLUUID LLPanelMainInventory::getSingleFolderViewRoot()
{
    return mCombinationInventoryPanel->getSingleFolderRoot();
}

void LLPanelMainInventory::showActionMenu(LLMenuGL* menu, std::string spawning_view_name)
{
    if (menu)
    {
        menu->buildDrawLabels();
        menu->updateParent(LLMenuGL::sMenuContainer);
        LLView* spawning_view = getChild<LLView> (spawning_view_name);
        S32 menu_x, menu_y;
        //show menu in co-ordinates of panel
        spawning_view->localPointToOtherView(0, 0, &menu_x, &menu_y, this);
        LLMenuGL::showPopup(this, menu, menu_x, menu_y);
    }
}

// <FS:Ansariel> Keep better inventory layout
void LLPanelMainInventory::onTrashButtonClick()
{
    onClipboardAction("delete");
}
// </FS:Ansariel>

void LLPanelMainInventory::onClipboardAction(const LLSD& userdata)
{
    std::string command_name = userdata.asString();
    getActivePanel()->doToSelected(command_name);
}

void LLPanelMainInventory::saveTexture(const LLSD& userdata)
{
    LLUUID item_id;
    if(mSingleFolderMode && isGalleryViewMode())
    {
        item_id = mCombinationGalleryPanel->getFirstSelectedItemID();
        if (item_id.isNull()) return;
    }
    else
    {
        LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
        if (!current_item)
        {
            return;
        }
        item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
    }

    LLPreviewTexture* preview_texture = LLFloaterReg::showTypedInstance<LLPreviewTexture>("preview_texture", LLSD(item_id), TAKE_FOCUS_YES);
    if (preview_texture)
    {
        preview_texture->openToSave();
    }
}

void LLPanelMainInventory::onCustomAction(const LLSD& userdata)
{
    if (!isActionEnabled(userdata))
        return;

    const std::string command_name = userdata.asString();

    if (command_name == "new_window")
    {
        newWindow();
    }
    if (command_name == "sort_by_name")
    {
        const LLSD arg = "name";
        setSortBy(arg);
    }
    if (command_name == "sort_by_recent")
    {
        const LLSD arg = "date";
        setSortBy(arg);
    }
    if (command_name == "sort_folders_by_name")
    {
        const LLSD arg = "foldersalwaysbyname";
        setSortBy(arg);
    }
    if (command_name == "sort_system_folders_to_top")
    {
        const LLSD arg = "systemfolderstotop";
        setSortBy(arg);
    }
    if (command_name == "add_objects_on_double_click")
    {
        gSavedSettings.setBOOL("FSDoubleClickAddInventoryObjects",!gSavedSettings.getBOOL("FSDoubleClickAddInventoryObjects"));
    }
    if (command_name == "add_clothing_on_double_click")
    {
        gSavedSettings.setBOOL("FSDoubleClickAddInventoryClothing",!gSavedSettings.getBOOL("FSDoubleClickAddInventoryClothing"));
    }
    if (command_name == "show_filters")
    {
        toggleFindOptions();
    }
    if (command_name == "reset_filters")
    {
        resetFilters();
    }
    if (command_name == "close_folders")
    {
        closeAllFolders();
    }
    if (command_name == "empty_trash")
    {
        const std::string notification = "ConfirmEmptyTrash";
        gInventory.emptyFolderType(notification, LLFolderType::FT_TRASH);
    }
    if (command_name == "empty_lostnfound")
    {
        const std::string notification = "ConfirmEmptyLostAndFound";
        gInventory.emptyFolderType(notification, LLFolderType::FT_LOST_AND_FOUND);
    }
    if (command_name == "save_texture")
    {
        saveTexture(userdata);
    }
    // This doesn't currently work, since the viewer can't change an assetID an item.
    if (command_name == "regenerate_link")
    {
        LLInventoryPanel *active_panel = getActivePanel();
        LLFolderViewItem* current_item = active_panel->getRootFolder()->getCurSelectedItem();
        if (!current_item)
        {
            return;
        }
        const LLUUID item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
        LLViewerInventoryItem *item = gInventory.getItem(item_id);
        if (item)
        {
            item->regenerateLink();
        }
        active_panel->setSelection(item_id, TAKE_FOCUS_NO);
    }
    if (command_name == "find_original")
    {
        if(mSingleFolderMode && isGalleryViewMode())
        {
            LLInventoryObject *obj = gInventory.getObject(mCombinationGalleryPanel->getFirstSelectedItemID());
            if (obj && obj->getIsLinkType())
            {
                show_item_original(obj->getUUID());
            }
        }
        else
        {
        LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
        if (!current_item)
        {
            return;
        }
        static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->performAction(getActivePanel()->getModel(), "goto");
        }
    }

    if (command_name == "find_links")
    {
        if(mSingleFolderMode && isGalleryViewMode())
        {
            LLFloaterSidePanelContainer* inventory_container = newWindow();
            if (inventory_container)
            {
                LLSidepanelInventory* sidepanel_inventory = dynamic_cast<LLSidepanelInventory*>(inventory_container->findChild<LLPanel>("main_panel", true));
                if (sidepanel_inventory)
                {
                    LLPanelMainInventory* main_inventory = sidepanel_inventory->getMainInventoryPanel();
                    if (main_inventory)
                    {
                        LLInventoryObject *obj = gInventory.getObject(mCombinationGalleryPanel->getFirstSelectedItemID());
                        if (obj)
                        {
                            main_inventory->findLinks(obj->getUUID(), obj->getName());
                        }
                    }
                }
            }
        }
        else
        {
            LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
            if (!current_item)
            {
                return;
            }
            const LLUUID& item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
            const std::string &item_name = current_item->getViewModelItem()->getName();
            findLinks(item_id, item_name);
        }
    }

    if (command_name == "replace_links")
    {
        LLSD params;
        if(mSingleFolderMode && isGalleryViewMode())
        {
            params = LLSD(mCombinationGalleryPanel->getFirstSelectedItemID());
        }
        else
        {
        LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
        if (current_item)
        {
            LLInvFVBridge* bridge = (LLInvFVBridge*)current_item->getViewModelItem();

            if (bridge)
            {
                LLInventoryObject* obj = bridge->getInventoryObject();
                if (obj && obj->getType() != LLAssetType::AT_CATEGORY && obj->getActualType() != LLAssetType::AT_LINK_FOLDER)
                {
                    params = LLSD(obj->getUUID());
                }
            }
        }
        }
        LLFloaterReg::showInstance("linkreplace", params);
    }

    if (command_name == "close_inv_windows")
    {
        LLFloaterReg::const_instance_list_t& inst_list = LLFloaterReg::getFloaterList("inventory");
        for (LLFloaterReg::const_instance_list_t::const_iterator iter = inst_list.begin(); iter != inst_list.end();)
        {
            LLFloaterSidePanelContainer* iv = dynamic_cast<LLFloaterSidePanelContainer*>(*iter++);
            if (iv)
            {
                iv->closeFloater();
            }
        }
        LLFloaterReg::hideInstance("inventory_settings");
    }

    if (command_name == "toggle_search_outfits")
    {
        getCurrentFilter().toggleSearchVisibilityOutfits();
    }

    if (command_name == "toggle_search_trash")
    {
        getCurrentFilter().toggleSearchVisibilityTrash();
    }

    if (command_name == "toggle_search_library")
    {
        getCurrentFilter().toggleSearchVisibilityLibrary();
    }

    if (command_name == "include_links")
    {
        getCurrentFilter().toggleSearchVisibilityLinks();
    }

    if (command_name == "share")
    {
        if(mSingleFolderMode && isGalleryViewMode())
        {
            std::set<LLUUID> uuids{ mCombinationGalleryPanel->getFirstSelectedItemID()};
            LLAvatarActions::shareWithAvatars(uuids, gFloaterView->getParentFloater(this));
        }
        else
        {
            LLAvatarActions::shareWithAvatars(this);
        }
    }
    if (command_name == "shop")
    {
        LLWeb::loadURL(gSavedSettings.getString("MarketplaceURL"));
    }
    if (command_name == "list_view")
    {
        setViewMode(MODE_LIST);
    }
    if (command_name == "gallery_view")
    {
        setViewMode(MODE_GALLERY);
    }
    if (command_name == "combination_view")
    {
        setViewMode(MODE_COMBINATION);
    }
}

void LLPanelMainInventory::onVisibilityChange( bool new_visibility )
{
    if(!new_visibility)
    {
        LLMenuGL* menu = (LLMenuGL*)mMenuAddHandle.get();
        if (menu)
        {
            menu->setVisible(false);
        }
        LLFolderView* root_folder = mActivePanel ? mActivePanel->getRootFolder() : nullptr;
        if (root_folder)
        {
            root_folder->finishRenamingItem();
        }
    }
}

bool LLPanelMainInventory::isSaveTextureEnabled(const LLSD& userdata)
{
    LLViewerInventoryItem *inv_item = NULL;
    if(mSingleFolderMode && isGalleryViewMode())
    {
        inv_item = gInventory.getItem(mCombinationGalleryPanel->getFirstSelectedItemID());
    }
    else
    {
        LLFolderView* root_folder = getActivePanel() ? getActivePanel()->getRootFolder() : nullptr;
        LLFolderViewItem* current_item = root_folder ? root_folder->getCurSelectedItem() : nullptr;
        if (current_item)
        {
            inv_item = dynamic_cast<LLViewerInventoryItem*>(static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getInventoryObject());
        }
    }
        if(inv_item)
        {
            bool can_save = inv_item->checkPermissionsSet(PERM_ITEM_UNRESTRICTED);
            LLInventoryType::EType curr_type = inv_item->getInventoryType();
            return can_save && (curr_type == LLInventoryType::IT_TEXTURE || curr_type == LLInventoryType::IT_SNAPSHOT);
        }

    return false;
}

bool LLPanelMainInventory::isActionEnabled(const LLSD& userdata)
{
    const std::string command_name = userdata.asString();
    // <FS:Ansariel> Unused changes from STORM-2091 that has been fixed by LL differently in the meantime
    //if (command_name == "not_empty")
    //{
    //  bool status = false;
    //  LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
    //  if (current_item)
    //  {
    //      const LLUUID& item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
    //      LLInventoryModel::cat_array_t* cat_array;
    //      LLInventoryModel::item_array_t* item_array;
    //      gInventory.getDirectDescendentsOf(item_id, cat_array, item_array);
    //      status = (0 == cat_array->size() && 0 == item_array->size());
    //  }
    //  return status;
    //}
    // </FS:Ansariel>
    if (command_name == "delete")
    {
        return getActivePanel()->isSelectionRemovable();
    }
    if (command_name == "save_texture")
    {
        return isSaveTextureEnabled(userdata);
    }
    if (command_name == "find_original")
    {
        LLUUID item_id;
        if(mSingleFolderMode && isGalleryViewMode())
        {
            item_id = mCombinationGalleryPanel->getFirstSelectedItemID();
        }
        else{
        LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
        if (!current_item) return false;
        item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
        }
        const LLViewerInventoryItem *item = gInventory.getItem(item_id);
        if (item && item->getIsLinkType() && !item->getIsBrokenLink())
        {
            return true;
        }
        return false;
    }

    if (command_name == "find_links")
    {
        LLUUID item_id;
        if(mSingleFolderMode && isGalleryViewMode())
        {
            item_id = mCombinationGalleryPanel->getFirstSelectedItemID();
        }
        else{
        LLFolderView* root = getActivePanel()->getRootFolder();
        std::set<LLFolderViewItem*> selection_set = root->getSelectionList();
        if (selection_set.size() != 1) return false;
        LLFolderViewItem* current_item = root->getCurSelectedItem();
        if (!current_item) return false;
        item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
        }
        const LLInventoryObject *obj = gInventory.getObject(item_id);
        if (obj && !obj->getIsLinkType() && LLAssetType::lookupCanLink(obj->getType()))
        {
            return true;
        }
        return false;
    }
    // This doesn't currently work, since the viewer can't change an assetID an item.
    if (command_name == "regenerate_link")
    {
        LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
        if (!current_item) return false;
        const LLUUID& item_id = static_cast<LLFolderViewModelItemInventory*>(current_item->getViewModelItem())->getUUID();
        const LLViewerInventoryItem *item = gInventory.getItem(item_id);
        if (item && item->getIsBrokenLink())
        {
            return true;
        }
        return false;
    }

    if (command_name == "share")
    {
        if(mSingleFolderMode && isGalleryViewMode())
        {
            return can_share_item(mCombinationGalleryPanel->getFirstSelectedItemID());
        }
        else{
        LLFolderViewItem* current_item = getActivePanel()->getRootFolder()->getCurSelectedItem();
        if (!current_item) return false;
        LLSidepanelInventory* parent = LLFloaterSidePanelContainer::getPanel<LLSidepanelInventory>("inventory");
        return parent ? parent->canShare() : false;
        }
    }
    if (command_name == "empty_trash")
    {
        const LLUUID &trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
        LLInventoryModel::EHasChildren children = gInventory.categoryHasChildren(trash_id);
        return children != LLInventoryModel::CHILDREN_NO && gInventory.isCategoryComplete(trash_id);
    }
    if (command_name == "empty_lostnfound")
    {
        const LLUUID &trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);
        LLInventoryModel::EHasChildren children = gInventory.categoryHasChildren(trash_id);
        return children != LLInventoryModel::CHILDREN_NO && gInventory.isCategoryComplete(trash_id);
    }

    return true;
}

bool LLPanelMainInventory::isActionVisible(const LLSD& userdata)
{
    const std::string param_str = userdata.asString();
    if (param_str == "single_folder_view")
    {
        return mSingleFolderMode;
    }
    if (param_str == "multi_folder_view")
    {
        return !mSingleFolderMode;
    }

    return true;
}

bool LLPanelMainInventory::isActionChecked(const LLSD& userdata)
{
    U32 sort_order_mask = (mSingleFolderMode && isGalleryViewMode()) ? mCombinationGalleryPanel->getSortOrder() :  getActivePanel()->getSortOrder();
    const std::string command_name = userdata.asString();
    if (command_name == "sort_by_name")
    {
        return ~sort_order_mask & LLInventoryFilter::SO_DATE;
    }

    if (command_name == "sort_by_recent")
    {
        return sort_order_mask & LLInventoryFilter::SO_DATE;
    }

    if (command_name == "sort_folders_by_name")
    {
        return sort_order_mask & LLInventoryFilter::SO_FOLDERS_BY_NAME;
    }

    if (command_name == "sort_system_folders_to_top")
    {
        return sort_order_mask & LLInventoryFilter::SO_SYSTEM_FOLDERS_TO_TOP;
    }

    if (command_name == "toggle_search_outfits")
    {
        return (getCurrentFilter().getSearchVisibilityTypes() & LLInventoryFilter::VISIBILITY_OUTFITS) != 0;
    }

    if (command_name == "toggle_search_trash")
    {
        return (getCurrentFilter().getSearchVisibilityTypes() & LLInventoryFilter::VISIBILITY_TRASH) != 0;
    }

    if (command_name == "toggle_search_library")
    {
        return (getCurrentFilter().getSearchVisibilityTypes() & LLInventoryFilter::VISIBILITY_LIBRARY) != 0;
    }

    if (command_name == "include_links")
    {
        return (getCurrentFilter().getSearchVisibilityTypes() & LLInventoryFilter::VISIBILITY_LINKS) != 0;
    }

    if (command_name == "list_view")
    {
        return isListViewMode();
    }
    if (command_name == "gallery_view")
    {
        return isGalleryViewMode();
    }
    if (command_name == "combination_view")
    {
        return isCombinationViewMode();
    }

    if (command_name == "add_objects_on_double_click")
    {
        return gSavedSettings.getBOOL("FSDoubleClickAddInventoryObjects");
    }

    if (command_name == "add_clothing_on_double_click")
    {
        return gSavedSettings.getBOOL("FSDoubleClickAddInventoryClothing");
    }


    return false;
}

// <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
void LLPanelMainInventory::onCoalescedObjectsToggled(const LLSD& userdata)
{
    const std::string command_name = userdata.asString();
    if (command_name == "coalesced_objects_only")
    {
        getActivePanel()->setFilterCoalescedObjects(!getActivePanel()->getFilterCoalescedObjects());
    }

    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    // Update the Filter Finder window with the change to the filters, so they can sync
    if (getFinder())
    {
        getFinder()->updateElementsFromFilter();
    }
    // <FS:minerjr> [FIRE-35042]
}

bool LLPanelMainInventory::isCoalescedObjectsChecked(const LLSD& userdata)
{
    const std::string command_name = userdata.asString();
    if (command_name == "coalesced_objects_only")
    {
        return getActivePanel()->getFilterCoalescedObjects();
    }

    return false;
}
// </FS:Zi>

// <FS:Zi> Filter Links Menu
void LLPanelMainInventory::onFilterLinksChecked(const LLSD& userdata)
{
    const std::string command_name = userdata.asString();
    if (command_name == "show_links")
    {
        getActivePanel()->setFilterLinks(LLInventoryFilter::FILTERLINK_INCLUDE_LINKS);
    }

    if (command_name == "only_links")
    {
        getActivePanel()->setFilterLinks(LLInventoryFilter::FILTERLINK_ONLY_LINKS);
    }

    if (command_name == "hide_links")
    {
        getActivePanel()->setFilterLinks(LLInventoryFilter::FILTERLINK_EXCLUDE_LINKS);
    }
    // <FS:minerjr> [FIRE-35042] Inventory - Only Coalesced Filter - More accessible
    // Update the Filter Finder window with the change to the filters, so they can sync
    if (getFinder())
    {
        getFinder()->updateElementsFromFilter();
    }
    // <FS:minerjr> [FIRE-35042]
}

bool LLPanelMainInventory::isFilterLinksChecked(const LLSD& userdata)
{
    const std::string command_name = userdata.asString();
    if (command_name == "show_links")
    {
        return (getActivePanel()->getFilter().getFilterLinks() == LLInventoryFilter::FILTERLINK_INCLUDE_LINKS);
    }

    if (command_name == "only_links")
    {
        return (getActivePanel()->getFilter().getFilterLinks() == LLInventoryFilter::FILTERLINK_ONLY_LINKS);
    }

    if (command_name == "hide_links")
    {
        return (getActivePanel()->getFilter().getFilterLinks() == LLInventoryFilter::FILTERLINK_EXCLUDE_LINKS);
    }

    return false;
}
// </FS:Zi> Filter Links Menu

// <FS:Zi> FIRE-1175 - Filter Permissions Menu
void LLPanelMainInventory::onFilterPermissionsChecked(const LLSD &userdata)
{
    U64 permissions = getActivePanel()->getFilterPermissions();

    const std::string command_name = userdata.asString();
    if (command_name == "only_modify")
    {
        getActivePanel()->setFilterPermissions((PermissionMask)(permissions ^ PERM_MODIFY));
    }

    if (command_name == "only_copy")
    {
        getActivePanel()->setFilterPermissions((PermissionMask)(permissions ^ PERM_COPY));
    }

    if (command_name == "only_transfer")
    {
        getActivePanel()->setFilterPermissions((PermissionMask)(permissions ^ PERM_TRANSFER));
    }

    if (getFinder())
    {
        getFinder()->updateElementsFromFilter();
    }
}

bool LLPanelMainInventory::isFilterPermissionsChecked(const LLSD &userdata)
{
    const std::string command_name = userdata.asString();
    if (command_name == "only_modify")
    {
        return (getActivePanel()->getFilter().getFilterPermissions() & PERM_MODIFY);
    }

    if (command_name == "only_copy")
    {
        return (getActivePanel()->getFilter().getFilterPermissions() & PERM_COPY);
    }

    if (command_name == "only_transfer")
    {
        return (getActivePanel()->getFilter().getFilterPermissions() & PERM_TRANSFER);
    }

    return false;
}
// </FS:Zi>

// <FS:Zi> Extended Inventory Search
void LLPanelMainInventory::onSearchTypeChecked(const LLSD& userdata)
{
    std::string new_type = userdata.asString();
    if (new_type == "search_by_name")
    {
        getActivePanel()->setSearchType(LLInventoryFilter::SEARCHTYPE_NAME);
    }
    if (new_type == "search_by_creator")
    {
        getActivePanel()->setSearchType(LLInventoryFilter::SEARCHTYPE_CREATOR);
    }
    if (new_type == "search_by_description")
    {
        getActivePanel()->setSearchType(LLInventoryFilter::SEARCHTYPE_DESCRIPTION);
    }
    if (new_type == "search_by_UUID")
    {
        getActivePanel()->setSearchType(LLInventoryFilter::SEARCHTYPE_UUID);
    }
    if (new_type == "search_by_all")
    {
        getActivePanel()->setSearchType(LLInventoryFilter::SEARCHTYPE_ALL);
    }
}

bool LLPanelMainInventory::isSearchTypeChecked(const LLSD& userdata)
{
    LLInventoryFilter::ESearchType search_type = getActivePanel()->getSearchType();
    const std::string command_name = userdata.asString();
    if (command_name == "search_by_name")
    {
        return (search_type == LLInventoryFilter::SEARCHTYPE_NAME);
    }

    if (command_name == "search_by_creator")
    {
        return (search_type == LLInventoryFilter::SEARCHTYPE_CREATOR);
    }

    if (command_name == "search_by_description")
    {
        return (search_type == LLInventoryFilter::SEARCHTYPE_DESCRIPTION);
    }

    if (command_name == "search_by_UUID")
    {
        return (search_type == LLInventoryFilter::SEARCHTYPE_UUID);
    }

    if (command_name == "search_by_all")
    {
        return (search_type == LLInventoryFilter::SEARCHTYPE_ALL);
    }
    return false;
}
// </FS:Zi> Extended Inventory Search

// <FS:Ansariel> Keep better inventory layout
bool LLPanelMainInventory::handleDragAndDropToTrash(bool drop, EDragAndDropType cargo_type, EAcceptance* accept)
{
    *accept = ACCEPT_NO;

    const bool is_enabled = isActionEnabled("delete");
    if (is_enabled) *accept = ACCEPT_YES_MULTI;

    if (is_enabled && drop)
    {
        onClipboardAction("delete");
    }
    return true;
}
// </FS:Ansariel>

void LLPanelMainInventory::setUploadCostIfNeeded()
{
    LLMenuGL* menu = (LLMenuGL*)mMenuAddHandle.get();
    if(mNeedUploadCost && menu)
    {
        const std::string sound_upload_cost_str = std::to_string(LLAgentBenefitsMgr::current().getSoundUploadCost());
        const std::string animation_upload_cost_str = std::to_string(LLAgentBenefitsMgr::current().getAnimationUploadCost());
        menu->getChild<LLView>("Upload Sound")->setLabelArg("[COST]", sound_upload_cost_str);
        menu->getChild<LLView>("Upload Animation")->setLabelArg("[COST]", animation_upload_cost_str);
    }
}

bool is_add_allowed(LLUUID folder_id)
{
    if(!gInventory.isObjectDescendentOf(folder_id, gInventory.getRootFolderID()))
    {
        return false;
    }

    std::vector<LLFolderType::EType> not_allowed_types;
    not_allowed_types.push_back(LLFolderType::FT_LOST_AND_FOUND);
    not_allowed_types.push_back(LLFolderType::FT_FAVORITE);
    not_allowed_types.push_back(LLFolderType::FT_MARKETPLACE_LISTINGS);
    not_allowed_types.push_back(LLFolderType::FT_TRASH);
    not_allowed_types.push_back(LLFolderType::FT_CURRENT_OUTFIT);
    not_allowed_types.push_back(LLFolderType::FT_INBOX);

    for (std::vector<LLFolderType::EType>::const_iterator it = not_allowed_types.begin();
         it != not_allowed_types.end(); ++it)
    {
        if(gInventory.isObjectDescendentOf(folder_id, gInventory.findCategoryUUIDForType(*it)))
        {
            return false;
        }
    }

    LLViewerInventoryCategory* cat = gInventory.getCategory(folder_id);
    if (cat && (cat->getPreferredType() == LLFolderType::FT_OUTFIT))
    {
        return false;
    }
    return true;
}

void LLPanelMainInventory::disableAddIfNeeded()
{
    LLMenuGL* menu = (LLMenuGL*)mMenuAddHandle.get();
    if (menu)
    {
        bool enable = !mSingleFolderMode || is_add_allowed(getCurrentSFVRoot());

        menu->getChild<LLMenuItemGL>("New Folder")->setEnabled(enable && !isRecentItemsPanelSelected());
        menu->getChild<LLMenuItemGL>("New Script")->setEnabled(enable);
        menu->getChild<LLMenuItemGL>("New Note")->setEnabled(enable);
        menu->getChild<LLMenuItemGL>("New Gesture")->setEnabled(enable);
        menu->setItemEnabled("New Clothes", enable);
        menu->setItemEnabled("New Body Parts", enable);
        menu->setItemEnabled("New Settings", enable);
    }
}

bool LLPanelMainInventory::hasSettingsInventory()
{
    return LLEnvironment::instance().isInventoryEnabled();
}

bool LLPanelMainInventory::hasMaterialsInventory()
{
    std::string agent_url = gAgent.getRegionCapability("UpdateMaterialAgentInventory");
    std::string task_url = gAgent.getRegionCapability("UpdateMaterialTaskInventory");

    return (!agent_url.empty() && !task_url.empty());
}

void LLPanelMainInventory::updateTitle()
{
    LLFloater* inventory_floater = gFloaterView->getParentFloater(this);
    if(inventory_floater)
    {
        if(mSingleFolderMode)
        {
            inventory_floater->setTitle(getLocalizedRootName());
            LLFloaterInventoryFinder *finder = getFinder();
            if (finder)
            {
                finder->setTitle(getLocalizedRootName());
            }
        }
        else
        {
            inventory_floater->setTitle(getString("inventory_title"));
        }
    }
    updateNavButtons();
}

void LLPanelMainInventory::onCombinationRootChanged(bool gallery_clicked)
{
    if(gallery_clicked)
    {
        mCombinationInventoryPanel->changeFolderRoot(mCombinationGalleryPanel->getRootFolder());
    }
    else
    {
        mCombinationGalleryPanel->setRootFolder(mCombinationInventoryPanel->getSingleFolderRoot());
    }
    mForceShowInvLayout = false;
    updateTitle();
    mReshapeInvLayout = true;
}

void LLPanelMainInventory::onCombinationGallerySelectionChanged(const LLUUID& category_id)
{
}

void LLPanelMainInventory::onCombinationInventorySelectionChanged(const std::deque<LLFolderViewItem*>& items, bool user_action)
{
    onSelectionChange(mCombinationInventoryPanel, items, user_action);
}

void LLPanelMainInventory::updatePanelVisibility()
{
    mDefaultViewPanel->setVisible(!mSingleFolderMode);
    mCombinationViewPanel->setVisible(mSingleFolderMode);
    mNavigationBtnsPanel->setVisible(mSingleFolderMode);
    mViewModeBtn->setImageOverlay(mSingleFolderMode ? getString("default_mode_btn") : getString("single_folder_mode_btn"));
    mViewModeBtn->setEnabled(mSingleFolderMode || (getAllItemsPanel() == getActivePanel()));
    if (mSingleFolderMode)
    {
        if (isCombinationViewMode())
        {
            LLInventoryFilter& comb_inv_filter = mCombinationInventoryPanel->getFilter();
            comb_inv_filter.setFilterThumbnails(LLInventoryFilter::FILTER_EXCLUDE_THUMBNAILS);
            comb_inv_filter.markDefault();

            LLInventoryFilter& comb_gallery_filter = mCombinationGalleryPanel->getFilter();
            comb_gallery_filter.setFilterThumbnails(LLInventoryFilter::FILTER_ONLY_THUMBNAILS);
            comb_gallery_filter.markDefault();

            // visibility will be controled by updateCombinationVisibility()
            mCombinationGalleryLayoutPanel->setVisible(true);
            mCombinationGalleryPanel->setVisible(true);
            mCombinationListLayoutPanel->setVisible(true);
        }
        else
        {
            LLInventoryFilter& comb_inv_filter = mCombinationInventoryPanel->getFilter();
            comb_inv_filter.setFilterThumbnails(LLInventoryFilter::FILTER_INCLUDE_THUMBNAILS);
            comb_inv_filter.markDefault();

            LLInventoryFilter& comb_gallery_filter = mCombinationGalleryPanel->getFilter();
            comb_gallery_filter.setFilterThumbnails(LLInventoryFilter::FILTER_INCLUDE_THUMBNAILS);
            comb_gallery_filter.markDefault();

            mCombinationLayoutStack->setPanelSpacing(0);
            mCombinationGalleryLayoutPanel->setVisible(mSingleFolderMode && isGalleryViewMode());
            mCombinationGalleryPanel->setVisible(mSingleFolderMode && isGalleryViewMode()); // to prevent or process updates
            mCombinationListLayoutPanel->setVisible(mSingleFolderMode && isListViewMode());
        }
    }
    else
    {
        mCombinationGalleryLayoutPanel->setVisible(false);
        mCombinationGalleryPanel->setVisible(false); // to prevent updates
        mCombinationListLayoutPanel->setVisible(false);
    }
}

void LLPanelMainInventory::updateCombinationVisibility()
{
    if(mSingleFolderMode && isCombinationViewMode())
    {
        bool is_gallery_empty = !mCombinationGalleryPanel->hasVisibleItems();
        bool show_inv_pane = mCombinationInventoryPanel->hasVisibleItems() || is_gallery_empty || mForceShowInvLayout;

        const S32 DRAG_HANDLE_PADDING = 12; // for drag handle to not overlap gallery when both inventories are visible
        mCombinationLayoutStack->setPanelSpacing(show_inv_pane ? DRAG_HANDLE_PADDING : 0);

        mCombinationGalleryLayoutPanel->setVisible(!is_gallery_empty);
        mCombinationListLayoutPanel->setVisible(show_inv_pane);
        LLFolderView* root_folder = mCombinationInventoryPanel->getRootFolder();
        if (root_folder)
        {
            root_folder->setForceArrange(!show_inv_pane);
            if (mCombinationInventoryPanel->hasVisibleItems())
            {
                mForceShowInvLayout = false;
            }
        }
        if(is_gallery_empty)
        {
            mCombinationGalleryPanel->handleModifiedFilter();
        }

        if (mReshapeInvLayout
            && show_inv_pane
            && (mCombinationGalleryPanel->hasVisibleItems() || mCombinationGalleryPanel->areViewsInitialized())
            && mCombinationInventoryPanel->areViewsInitialized())
        {
            mReshapeInvLayout = false;

            // force drop previous shape (because panel doesn't decrease shape properly)
            LLRect list_latout = mCombinationListLayoutPanel->getRect();
            list_latout.mTop = list_latout.mBottom; // min height is at 100, so it should snap to be bigger
            mCombinationListLayoutPanel->setShape(list_latout, false);

            LLRect inv_inner_rect = mCombinationInventoryPanel->getScrollableContainer()->getScrolledViewRect();
            S32 inv_height = inv_inner_rect.getHeight()
                + (mCombinationInventoryPanel->getScrollableContainer()->getBorderWidth() * 2)
                + mCombinationInventoryPanel->getScrollableContainer()->getSize();
            LLRect inner_galery_rect = mCombinationGalleryPanel->getScrollableContainer()->getScrolledViewRect();
            S32 gallery_height = inner_galery_rect.getHeight()
                + (mCombinationGalleryPanel->getScrollableContainer()->getBorderWidth() * 2)
                + mCombinationGalleryPanel->getScrollableContainer()->getSize();
            LLRect layout_rect = mCombinationViewPanel->getRect();

            // by default make it take 1/3 of the panel
            S32 list_default_height = layout_rect.getHeight() / 3;
            // Don't set height from gallery_default_height - needs to account for a resizer in such case
            S32 gallery_default_height = layout_rect.getHeight() - list_default_height;

            if (inv_height > list_default_height
                && gallery_height < gallery_default_height)
            {
                LLRect gallery_latout = mCombinationGalleryLayoutPanel->getRect();
                gallery_latout.mTop = gallery_latout.mBottom + gallery_height;
                mCombinationGalleryLayoutPanel->setShape(gallery_latout, true /*tell stack to account for new shape*/);
            }
            else if (inv_height < list_default_height
                     && gallery_height > gallery_default_height)
            {
                LLRect list_latout = mCombinationListLayoutPanel->getRect();
                list_latout.mTop = list_latout.mBottom + inv_height;
                mCombinationListLayoutPanel->setShape(list_latout, true /*tell stack to account for new shape*/);
            }
            else
            {
                LLRect list_latout = mCombinationListLayoutPanel->getRect();
                list_latout.mTop = list_latout.mBottom + list_default_height;
                mCombinationListLayoutPanel->setShape(list_latout, true /*tell stack to account for new shape*/);
            }
        }
    }

    if (mSingleFolderMode
        && !isGalleryViewMode()
        && mCombInvUUIDNeedsRename.notNull()
        && mCombinationInventoryPanel->areViewsInitialized())
    {
        mCombinationInventoryPanel->setSelectionByID(mCombInvUUIDNeedsRename, true);
        LLFolderView* root = mCombinationInventoryPanel->getRootFolder();
        if (root)
        {
            root->scrollToShowSelection();
            root->setNeedsAutoRename(true);
        }
        mCombInvUUIDNeedsRename.setNull();
    }
}

void LLPanelMainInventory::updateNavButtons()
{
    if(isListViewMode())
    {
        mBackBtn->setEnabled(mCombinationInventoryPanel->isBackwardAvailable());
        mForwardBtn->setEnabled(mCombinationInventoryPanel->isForwardAvailable());
    }
    if(isGalleryViewMode())
    {
        mBackBtn->setEnabled(mCombinationGalleryPanel->isBackwardAvailable());
        mForwardBtn->setEnabled(mCombinationGalleryPanel->isForwardAvailable());
    }
    if(isCombinationViewMode())
    {
        mBackBtn->setEnabled(mCombinationInventoryPanel->isBackwardAvailable());
        mForwardBtn->setEnabled(mCombinationInventoryPanel->isForwardAvailable());
    }

    const LLViewerInventoryCategory* cat = gInventory.getCategory(getCurrentSFVRoot());
    bool up_enabled = (cat && cat->getParentUUID().notNull());
    mUpBtn->setEnabled(up_enabled);
}

LLSidepanelInventory* LLPanelMainInventory::getParentSidepanelInventory()
{
    LLFloaterSidePanelContainer* inventory_container = dynamic_cast<LLFloaterSidePanelContainer*>(gFloaterView->getParentFloater(this));
    if(inventory_container)
    {
        return dynamic_cast<LLSidepanelInventory*>(inventory_container->findChild<LLPanel>("main_panel", true));
    }
    return NULL;
}

void LLPanelMainInventory::setViewMode(EViewModeType mode)
{
    if(mode != mViewMode)
    {
        std::list<LLUUID> forward_history;
        std::list<LLUUID> backward_history;
        U32 sort_order = 0;
        switch(mViewMode)
        {
            case MODE_LIST:
                forward_history = mCombinationInventoryPanel->getNavForwardList();
                backward_history = mCombinationInventoryPanel->getNavBackwardList();
                sort_order = mCombinationInventoryPanel->getSortOrder();
                break;
            case MODE_GALLERY:
                forward_history = mCombinationGalleryPanel->getNavForwardList();
                backward_history = mCombinationGalleryPanel->getNavBackwardList();
                sort_order = mCombinationGalleryPanel->getSortOrder();
                break;
            case MODE_COMBINATION:
                forward_history = mCombinationInventoryPanel->getNavForwardList();
                backward_history = mCombinationInventoryPanel->getNavBackwardList();
                mCombinationInventoryPanel->getRootFolder()->setForceArrange(false);
                sort_order = mCombinationInventoryPanel->getSortOrder();
                break;
        }

        LLUUID cur_root = getCurrentSFVRoot();
        mViewMode = mode;

        updatePanelVisibility();

        if(isListViewMode())
        {
            mCombinationInventoryPanel->changeFolderRoot(cur_root);
            mCombinationInventoryPanel->setNavForwardList(forward_history);
            mCombinationInventoryPanel->setNavBackwardList(backward_history);
            mCombinationInventoryPanel->setSortOrder(sort_order);
        }
        if(isGalleryViewMode())
        {
            mCombinationGalleryPanel->setRootFolder(cur_root);
            mCombinationGalleryPanel->setNavForwardList(forward_history);
            mCombinationGalleryPanel->setNavBackwardList(backward_history);
            mCombinationGalleryPanel->setSortOrder(sort_order, true);
        }
        if(isCombinationViewMode())
        {
            mCombinationInventoryPanel->changeFolderRoot(cur_root);
            mCombinationGalleryPanel->setRootFolder(cur_root);
            mCombinationInventoryPanel->setNavForwardList(forward_history);
            mCombinationInventoryPanel->setNavBackwardList(backward_history);
            mCombinationGalleryPanel->setNavForwardList(forward_history);
            mCombinationGalleryPanel->setNavBackwardList(backward_history);
            mCombinationInventoryPanel->setSortOrder(sort_order);
            mCombinationGalleryPanel->setSortOrder(sort_order, true);
        }

        updateNavButtons();

        onFilterSelected();
        if((isListViewMode() && (mActivePanel->getFilterSubString() != mFilterSubString)) ||
           (isGalleryViewMode() && (mCombinationGalleryPanel->getFilterSubString() != mFilterSubString)))
        {
            onFilterEdit(mFilterSubString);
        }
    }
}

std::string LLPanelMainInventory::getLocalizedRootName()
{
    return mSingleFolderMode ? get_localized_folder_name(getCurrentSFVRoot()) : "";
}

LLUUID LLPanelMainInventory::getCurrentSFVRoot()
{
    if(isListViewMode())
    {
        return mCombinationInventoryPanel->getSingleFolderRoot();
    }
    if(isGalleryViewMode())
    {
        return mCombinationGalleryPanel->getRootFolder();
    }
    if(isCombinationViewMode())
    {
        return mCombinationInventoryPanel->getSingleFolderRoot();
    }
    return LLUUID::null;
}

LLInventoryFilter& LLPanelMainInventory::getCurrentFilter()
{
    if(mSingleFolderMode && isGalleryViewMode())
    {
        return mCombinationGalleryPanel->getFilter();
    }
    else
    {
        return mActivePanel->getFilter();
    }
}

void LLPanelMainInventory::setGallerySelection(const LLUUID& item_id, bool new_window)
{
    if(mSingleFolderMode && isGalleryViewMode())
    {
        mCombinationGalleryPanel->changeItemSelection(item_id, true);
    }
    else if(mSingleFolderMode && isCombinationViewMode())
    {
        if(mCombinationGalleryPanel->getFilter().checkAgainstFilterThumbnails(item_id))
        {
            mCombinationGalleryPanel->changeItemSelection(item_id, false);
            scrollToGallerySelection();
        }
        else
        {
            mCombinationInventoryPanel->setSelection(item_id, true);
            scrollToInvPanelSelection();
        }
    }
    else if (mSingleFolderMode && isListViewMode())
    {
        mCombinationInventoryPanel->setSelection(item_id, true);
    }
}

void LLPanelMainInventory::scrollToGallerySelection()
{
    mCombinationGalleryPanel->scrollToShowItem(mCombinationGalleryPanel->getFirstSelectedItemID());
}

void LLPanelMainInventory::scrollToInvPanelSelection()
{
    mCombinationInventoryPanel->getRootFolder()->scrollToShowSelection();
}

// List Commands                                                              //
////////////////////////////////////////////////////////////////////////////////
