/**
* @file llinventoryfilter.cpp
* @brief Support for filtering your inventory to only display a subset of the
* available items.
*
* $LicenseInfo:firstyear=2005&license=viewerlgpl$
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

#include "llinventoryfilter.h"

// viewer includes
#include "llagent.h"
#include "llfolderviewmodel.h"
#include "llfolderviewitem.h"
#include "llinventorymodel.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llinventoryfunctions.h"
#include "llmarketplacefunctions.h"
#include "llregex.h"
#include "llviewercontrol.h"
#include "llfolderview.h"
#include "llinventorybridge.h"
#include "llviewerfoldertype.h"
#include "llradiogroup.h"
#include "llstartup.h"

// linden library includes
#include "llclipboard.h"
#include "lltrans.h"

// Firestorm includes
#include "llappearancemgr.h" // needed to query whether we are in COF
#ifdef OPENSIM
#include "fsgridhandler.h" // <FS:Beq> need to check if in opensim
#endif

#include "llinventorydefines.h"     // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects

LLInventoryFilter::FilterOps::FilterOps(const Params& p)
:   mFilterObjectTypes(p.object_types),
    mFilterCategoryTypes(p.category_types),
    mFilterWearableTypes(p.wearable_types),
    mFilterSettingsTypes(p.settings_types),
    mMinDate(p.date_range.min_date),
    mMaxDate(p.date_range.max_date),
    mHoursAgo(p.hours_ago),
    mDateSearchDirection(p.date_search_direction),
    mShowFolderState(p.show_folder_state),
    mFilterCreatorType(p.creator_type),
    mPermissions(p.permissions),
    mFilterTypes(p.types),
    mFilterUUID(p.uuid),
    mFilterLinks(p.links),
    mFilterThumbnails(p.thumbnails),
    mCoalescedObjectsOnly(p.coalesced_objects_only),        // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
    mSearchVisibility(p.search_visibility)
{
}

///----------------------------------------------------------------------------
/// Class LLInventoryFilter
///----------------------------------------------------------------------------
LLInventoryFilter::LLInventoryFilter(const Params& p)
:   mName(p.name),
    mFilterModified(FILTER_NONE),
    mEmptyLookupMessage("InventoryNoMatchingItems"),
    mDefaultEmptyLookupMessage(""),
    mFilterOps(p.filter_ops),
    mBackupFilterOps(mFilterOps),
    mFilterSubString(p.substring),
    mCurrentGeneration(0),
    mFirstRequiredGeneration(0),
    mFirstSuccessGeneration(0),
    mSearchType(SEARCHTYPE_NAME),
    mSingleFolderMode(false)
{
    // copy mFilterOps into mDefaultFilterOps
    markDefault();
    mUsername = gAgentUsername;
    LLStringUtil::toUpper(mUsername);
}

bool LLInventoryFilter::check(const LLFolderViewModelItem* item)
{
    const LLFolderViewModelItemInventory* listener = dynamic_cast<const LLFolderViewModelItemInventory*>(item);

    // If it's a folder and we're showing all folders, return automatically.
    const bool is_folder = listener->getInventoryType() == LLInventoryType::IT_CATEGORY;
    if (is_folder && (mFilterOps.mShowFolderState == LLInventoryFilter::SHOW_ALL_FOLDERS))
    {
        return true;
    }

    std::string desc = listener->getSearchableCreatorName();
    switch (mSearchType)
    {
        case SEARCHTYPE_CREATOR:
            desc = listener->getSearchableCreatorName();
            break;
        case SEARCHTYPE_DESCRIPTION:
            desc = listener->getSearchableDescription();
            break;
        case SEARCHTYPE_UUID:
            desc = listener->getSearchableUUIDString();
            break;
        // <FS:Ansariel> Allow searching by all
        case SEARCHTYPE_ALL:
            desc = listener->getSearchableAll();
            break;
        // </FS:Ansariel>
        case SEARCHTYPE_NAME:
        default:
            desc = listener->getSearchableName();
            break;
    }

    bool passed = true;
    // <FS:Ansariel> Allow searching by all
    //if (!mExactToken.empty() && (mSearchType == SEARCHTYPE_NAME))
    if (!mExactToken.empty() && ((mSearchType == SEARCHTYPE_NAME) || (mSearchType == SEARCHTYPE_ALL)))
    // </FS:Ansariel>
    {
        passed = false;
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(" ");
        tokenizer tokens(desc, sep);

        for (const auto& token_iter : tokens)
        {
            if (token_iter == mExactToken)
            {
                passed = true;
                break;
            }
        }
    }
    // <FS:Ansariel> Allow searching by all
    //else if (mFilterTokens.size() > 0 && mSearchType == SEARCHTYPE_NAME)
    else if (mFilterTokens.size() > 0 && (mSearchType == SEARCHTYPE_NAME || mSearchType == SEARCHTYPE_ALL))
    // </FS:Ansariel>
    {
        for (const auto& token_iter : mFilterTokens)
        {
            if (desc.find(token_iter) == std::string::npos)
            {
                return false;
            }
        }
    }
    else
    {
        passed = checkAgainstFilterSubString(desc);
    }

    passed = passed && checkAgainstFilterType(listener);
    passed = passed && checkAgainstPermissions(listener);
    passed = passed && checkAgainstFilterLinks(listener);
    passed = passed && checkAgainstCreator(listener);
    passed = passed && checkAgainstSearchVisibility(listener);

    passed = passed && checkAgainstFilterThumbnails(listener->getUUID());

    return passed;
}

bool LLInventoryFilter::check(const LLInventoryItem* item)
{
    const bool passed_string = checkAgainstFilterSubString(item->getName());
    const bool passed_filtertype = checkAgainstFilterType(item);
    const bool passed_permissions = checkAgainstPermissions(item);

    return passed_filtertype && passed_permissions && passed_string;
}

bool LLInventoryFilter::checkFolder(const LLFolderViewModelItem* item) const
{
    const LLFolderViewModelItemInventory* listener = dynamic_cast<const LLFolderViewModelItemInventory*>(item);
    if (!listener)
    {
        LL_ERRS() << "Folder view event listener not found." << LL_ENDL;
        return false;
    }

    const LLUUID folder_id = listener->getUUID();

    return checkFolder(folder_id);
}

bool LLInventoryFilter::checkFolder(const LLUUID& folder_id) const
{
    // we're showing all folders, overriding filter
    if (mFilterOps.mShowFolderState == LLInventoryFilter::SHOW_ALL_FOLDERS)
    {
        return true;
    }

    // when applying a filter, matching folders get their contents downloaded first
    // but make sure we are not interfering with pre-download
    if (isNotDefault()
        && LLStartUp::getStartupState() > STATE_WEARABLES_WAIT
        && !LLInventoryModelBackgroundFetch::instance().inventoryFetchInProgress())
    {
        LLViewerInventoryCategory* cat = gInventory.getCategory(folder_id);
        if ((!cat && folder_id.notNull()))
        {
            // Shouldn't happen? Server provides full list of folders on startup
            LLInventoryModelBackgroundFetch::instance().start(folder_id, false);
        }
        else if (cat && cat->getVersion() == LLViewerInventoryCategory::VERSION_UNKNOWN)
        {
            // At the moment background fetch only cares about VERSION_UNKNOWN,
            // so do not check isCategoryComplete that compares descendant count,
            // but if that is nesesary, do a forced scheduleFolderFetch.
            cat->fetch();
        }
    }

    if (!checkAgainstFilterThumbnails(folder_id))
    {
        return false;
    }

    // Marketplace folder filtering
    const U32 filterTypes = mFilterOps.mFilterTypes;
    const U32 marketplace_filter = FILTERTYPE_MARKETPLACE_ACTIVE | FILTERTYPE_MARKETPLACE_INACTIVE |
                                   FILTERTYPE_MARKETPLACE_UNASSOCIATED | FILTERTYPE_MARKETPLACE_LISTING_FOLDER |
                                   FILTERTYPE_NO_MARKETPLACE_ITEMS;
    if (filterTypes & marketplace_filter)
    {
        S32 depth = depth_nesting_in_marketplace(folder_id);

        if (filterTypes & FILTERTYPE_NO_MARKETPLACE_ITEMS)
        {
            if (depth >= 0)
            {
                return false;
            }
        }

        if (filterTypes & FILTERTYPE_MARKETPLACE_LISTING_FOLDER)
        {
            if (depth > 1)
            {
                return false;
            }
        }

        if (depth > 0)
        {
            LLUUID listing_uuid = nested_parent_id(folder_id, depth);
            if (filterTypes & FILTERTYPE_MARKETPLACE_ACTIVE)
            {
                if (!LLMarketplaceData::instance().getActivationState(listing_uuid))
                {
                    return false;
                }
            }
            else if (filterTypes & FILTERTYPE_MARKETPLACE_INACTIVE)
            {
                if (!LLMarketplaceData::instance().isListed(listing_uuid) || LLMarketplaceData::instance().getActivationState(listing_uuid))
                {
                    return false;
                }
            }
            else if (filterTypes & FILTERTYPE_MARKETPLACE_UNASSOCIATED)
            {
                if (LLMarketplaceData::instance().isListed(listing_uuid))
                {
                    return false;
                }
            }
        }
    }

    // show folder links
    LLViewerInventoryItem* item = gInventory.getItem(folder_id);
    if (item && item->getActualType() == LLAssetType::AT_LINK_FOLDER)
    {
        return true;
    }

    if (mFilterOps.mFilterTypes & FILTERTYPE_CATEGORY)
    {
        // Can only filter categories for items in your inventory
        // (e.g. versus in-world object contents).
        const LLViewerInventoryCategory *cat = gInventory.getCategory(folder_id);
        if (!cat)
            return folder_id.isNull();
        LLFolderType::EType cat_type = cat->getPreferredType();
// <FS:Beq> FIRE-30501 suitcase missing on 32 bit viewer.
#ifdef OPENSIM
        if (LLGridManager::getInstance()->isInOpenSim() && cat_type == LLFolderType::FT_MY_SUITCASE)
        {
            return true; // suitcase will always be shown
        }
#endif
// </FS:Beq>
        if (cat_type != LLFolderType::FT_NONE && (1LL << cat_type & mFilterOps.mFilterCategoryTypes) == U64(0))
            return false;
    }

    return true;
}

bool LLInventoryFilter::checkAgainstFilterSubString(const std::string& desc) const
{
    if (mFilterSubString.empty())
        return true;

    size_t pos = desc.find(mFilterSubString);
    return pos != std::string::npos;
}

bool LLInventoryFilter::checkAgainstFilterType(const LLFolderViewModelItemInventory* listener) const
{
    if (!listener)
        return false;

    LLInventoryType::EType object_type = listener->getInventoryType();
    const LLUUID object_id = listener->getUUID();
    const LLInventoryObject *object = gInventory.getObject(object_id);

    const U32 filterTypes = mFilterOps.mFilterTypes;

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_OBJECT
    // Pass if this item's type is of the correct filter type
    if (filterTypes & FILTERTYPE_OBJECT)
    {
        switch (object_type)
        {
        case LLInventoryType::IT_NONE:
            // If it has no type, pass it, unless it's a link.
            if (object && object->getIsLinkType())
            {
                return false;
            }
            break;
        case LLInventoryType::IT_UNKNOWN:
            {
                // Unknows are only shown when we show every type.
                // Unknows are 255 and won't fit in 64 bits.
                if (mFilterOps.mFilterObjectTypes != 0xffffffffffffffffULL)
                {
                    return false;
                }
                break;
            }
        default:
            // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
            if (mFilterOps.mCoalescedObjectsOnly)
            {
                // only bother to get the inventory item pointer if we are actually showing only coalesced objects
                LLInventoryItem* item = gInventory.getItem(object_id);
                if (item)
                {
                    if (!(item->getFlags() & LLInventoryItemFlags::II_FLAGS_OBJECT_HAS_MULTIPLE_ITEMS))
                    {
                        return false;
                    }
                }
            }
            // </ FS:Zi>

            if ((1LL << object_type & mFilterOps.mFilterObjectTypes) == U64(0))
            {
                return false;
            }
            break;
        }
    }

    // <FS>
    //if (filterTypes & FILTERTYPE_WORN)
    //{
    //  if (!get_is_item_worn(object_id))
    //  {
    //      return false;
    //  }
    //}
    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_WORN
    // Pass if this item is worn (hiding COF and Outfits folders)
    if (filterTypes & FILTERTYPE_WORN)
    {
        if (!object)
        {
            return false;
        }
        const LLUUID& cat_id = object->getParentUUID();
        const LLViewerInventoryCategory* cat = gInventory.getCategory(cat_id);
        return !LLAppearanceMgr::instance().getIsInCOF(object_id)           // Not a link in COF
            && (!cat || cat->getPreferredType() != LLFolderType::FT_OUTFIT) // Not a link in an outfit folder
            && get_is_item_worn(object_id);
    }
    // </FS>


    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_UUID
    // Pass if this item is the target UUID or if it links to the target UUID
    if (filterTypes & FILTERTYPE_UUID)
    {
        if (!object)
            return false;

        if (object->getLinkedUUID() != mFilterOps.mFilterUUID)
            return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_DATE
    // Pass if this item is within the date range.
    if (filterTypes & FILTERTYPE_DATE)
    {
        const U16 HOURS_TO_SECONDS = 3600;
        time_t earliest = time_corrected() - (U64)mFilterOps.mHoursAgo * HOURS_TO_SECONDS;

        if (mFilterOps.mMinDate > time_min() && mFilterOps.mMinDate < earliest)
        {
            earliest = mFilterOps.mMinDate;
        }
        else if (!mFilterOps.mHoursAgo)
        {
            earliest = 0;
        }

        if (FILTERDATEDIRECTION_NEWER == mFilterOps.mDateSearchDirection || isSinceLogoff())
        {
            if (listener->getCreationDate() < earliest ||
                listener->getCreationDate() > mFilterOps.mMaxDate)
                return false;
        }
        else
        {
            if (listener->getCreationDate() > earliest ||
                listener->getCreationDate() > mFilterOps.mMaxDate)
                return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_WEARABLE
    // Pass if this item is a wearable of the appropriate type
    if (filterTypes & FILTERTYPE_WEARABLE)
    {
        LLWearableType::EType type = listener->getWearableType();
        if ((object_type == LLInventoryType::IT_WEARABLE) &&
                (((0x1LL << type) & mFilterOps.mFilterWearableTypes) == 0))
        {
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_SETTINGS
    // Pass if this item is a setting of the appropriate type
    if (filterTypes & FILTERTYPE_SETTINGS)
    {
        LLSettingsType::type_e type = listener->getSettingsType();
        if ((object_type == LLInventoryType::IT_SETTINGS) &&
            (((0x1LL << type) & mFilterOps.mFilterSettingsTypes) == 0))
        {
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_EMPTYFOLDERS
    // Pass if this item is a folder and is not a system folder that should be hidden
    if (filterTypes & FILTERTYPE_EMPTYFOLDERS)
    {
        if (object_type == LLInventoryType::IT_CATEGORY)
        {
            bool is_hidden_if_empty = LLViewerFolderType::lookupIsHiddenIfEmpty(listener->getPreferredType());
            if (is_hidden_if_empty)
            {
                // Force the fetching of those folders so they are hidden if they really are empty...
                // But don't interfere with startup download
                if (LLStartUp::getStartupState() > STATE_WEARABLES_WAIT)
                {
                    gInventory.fetchDescendentsOf(object_id);
                }

                LLInventoryModel::cat_array_t* cat_array = NULL;
                LLInventoryModel::item_array_t* item_array = NULL;
                gInventory.getDirectDescendentsOf(object_id,cat_array,item_array);
                size_t descendents_actual = 0;
                if(cat_array && item_array)
                {
                    descendents_actual = cat_array->size() + item_array->size();
                }
                if (descendents_actual == 0)
                {
                    return false;
                }
            }
        }
    }

    return true;
}

bool LLInventoryFilter::checkAgainstFilterType(const LLInventoryItem* item) const
{
    LLInventoryType::EType object_type = item->getInventoryType();

    const U32 filterTypes = mFilterOps.mFilterTypes;

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_OBJECT
    // Pass if this item's type is of the correct filter type
    if (filterTypes & FILTERTYPE_OBJECT)
    {
        switch (object_type)
        {
        case LLInventoryType::IT_NONE:
            // If it has no type, pass it, unless it's a link.
            if (item && item->getIsLinkType())
            {
                return false;
            }
            break;
        case LLInventoryType::IT_UNKNOWN:
            {
                // Unknows are only shown when we show every type.
                // Unknows are 255 and won't fit in 64 bits.
                if (mFilterOps.mFilterObjectTypes != 0xffffffffffffffffULL)
                {
                    return false;
                }
                break;
            }
        default:
            if ((1LL << object_type & mFilterOps.mFilterObjectTypes) == U64(0))
            {
                return false;
            }
            break;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_UUID
    // Pass if this item is the target UUID or if it links to the target UUID
    if (filterTypes & FILTERTYPE_UUID)
    {
        if (!item)
            return false;

        if (item->getLinkedUUID() != mFilterOps.mFilterUUID)
            return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FILTERTYPE_DATE
    // Pass if this item is within the date range.
    if (filterTypes & FILTERTYPE_DATE)
    {
        // We don't get the updated item creation date for the task inventory or
        // a notecard embedded item. See LLTaskInvFVBridge::getCreationDate().
        return false;
    }

    return true;
}

// Items and folders that are on the clipboard or, recursively, in a folder which
// is on the clipboard must be filtered out if the clipboard is in the "cut" mode.
bool LLInventoryFilter::checkAgainstClipboard(const LLUUID& object_id) const
{
    if (LLClipboard::instance().isCutMode())
    {
        LL_PROFILE_ZONE_SCOPED;
        LLUUID current_id = object_id;
        LLInventoryObject *current_object = gInventory.getObject(object_id);
        while (current_id.notNull() && current_object)
        {
            if (LLClipboard::instance().isOnClipboard(current_id))
            {
                return false;
            }
            current_id = current_object->getParentUUID();
            if (current_id.notNull())
            {
                current_object = gInventory.getObject(current_id);
            }
        }
    }
    return true;
}

bool LLInventoryFilter::checkAgainstPermissions(const LLFolderViewModelItemInventory* listener) const
{
    if (!listener)
        return false;

    PermissionMask perm = listener->getPermissionMask();
    const LLInvFVBridge *bridge = dynamic_cast<const LLInvFVBridge *>(listener);
    if (bridge && bridge->isLink())
    {
        const LLUUID& linked_uuid = gInventory.getLinkedItemID(bridge->getUUID());
        const LLViewerInventoryItem *linked_item = gInventory.getItem(linked_uuid);
        if (linked_item)
            perm = linked_item->getPermissionMask();
    }
    return (perm & mFilterOps.mPermissions) == mFilterOps.mPermissions;
}

bool LLInventoryFilter::checkAgainstPermissions(const LLInventoryItem* item) const
{
    if (!item)
        return false;

    LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
    PermissionMask perm = new_item->getPermissionMask();
    new_item = NULL;

    return (perm & mFilterOps.mPermissions) == mFilterOps.mPermissions;
}

bool LLInventoryFilter::checkAgainstFilterLinks(const LLFolderViewModelItemInventory* listener) const
{
    if (!listener)
        return true;

    const LLUUID object_id = listener->getUUID();
    const LLInventoryObject *object = gInventory.getObject(object_id);
    if (!object)
        return true;

    const bool is_link = object->getIsLinkType();
    if (is_link && (mFilterOps.mFilterLinks == FILTERLINK_EXCLUDE_LINKS))
        return false;
    if (!is_link && (mFilterOps.mFilterLinks == FILTERLINK_ONLY_LINKS))
        return false;
    return true;
}

bool LLInventoryFilter::checkAgainstFilterThumbnails(const LLUUID& object_id) const
{
    const LLInventoryObject *object = gInventory.getObject(object_id);
    if (!object)
        return true;

    const bool is_thumbnail = object->getThumbnailUUID().notNull();
    if (is_thumbnail && (mFilterOps.mFilterThumbnails == FILTER_EXCLUDE_THUMBNAILS))
        return false;
    if (!is_thumbnail && (mFilterOps.mFilterThumbnails == FILTER_ONLY_THUMBNAILS))
        return false;
    return true;
}

bool LLInventoryFilter::checkAgainstCreator(const LLFolderViewModelItemInventory* listener) const
{
    if (!listener)
        return true;

    const bool is_folder = listener->getInventoryType() == LLInventoryType::IT_CATEGORY;
    switch (mFilterOps.mFilterCreatorType)
    {
        case FILTERCREATOR_SELF:
            if (is_folder)
                return false;
            return listener->getSearchableCreatorName() == mUsername;
        case FILTERCREATOR_OTHERS:
            if (is_folder)
                return false;
            return listener->getSearchableCreatorName() != mUsername;
        case FILTERCREATOR_ALL:
        default:
            return true;
    }
}

bool LLInventoryFilter::checkAgainstSearchVisibility(const LLFolderViewModelItemInventory* listener) const
{
    if (!listener || !hasFilterString()) return true;

    const LLUUID object_id = listener->getUUID();
    const LLInventoryObject *object = gInventory.getObject(object_id);
    if (!object)
        return true;

// <AS:chanayane> Short circuit search optimisation
    //const bool is_link = object->getIsLinkType();
    //if (is_link && ((mFilterOps.mSearchVisibility & VISIBILITY_LINKS) == 0))
    if (((mFilterOps.mSearchVisibility & VISIBILITY_LINKS) == 0) && object->getIsLinkType())
// </AS:chanayane>
        return false;

// <AS:chanayane> Short circuit search optimisation
    //if (listener->isItemInOutfits() && ((mFilterOps.mSearchVisibility & VISIBILITY_OUTFITS) == 0))
    if (((mFilterOps.mSearchVisibility & VISIBILITY_OUTFITS) == 0) && listener->isItemInOutfits())
// </AS:chanayane>
        return false;

// <AS:chanayane> Short circuit search optimisation
    //if (listener->isItemInTrash() && ((mFilterOps.mSearchVisibility & VISIBILITY_TRASH) == 0))
    if (((mFilterOps.mSearchVisibility & VISIBILITY_TRASH) == 0) && listener->isItemInTrash())
// </AS:chanayane>
        return false;

// <AS:chanayane> Short circuit search optimisation
    //if (!listener->isAgentInventory() && ((mFilterOps.mSearchVisibility & VISIBILITY_LIBRARY) == 0))
    if (((mFilterOps.mSearchVisibility & VISIBILITY_LIBRARY) == 0) && !listener->isAgentInventory())
// </AS:chanayane>
        return false;

    return true;
}

const std::string& LLInventoryFilter::getFilterSubString(bool trim) const
{
    return mFilterSubString;
}

std::string::size_type LLInventoryFilter::getStringMatchOffset(LLFolderViewModelItem* item) const
{
    // <FS:Ansariel> Zi's extended inventory search
    //if (mSearchType == SEARCHTYPE_NAME)
    if (mSearchType == SEARCHTYPE_NAME || mSearchType == SEARCHTYPE_ALL)
    // </FS:Ansariel>
    {
        return mFilterSubString.size() ? item->getSearchableName().find(mFilterSubString) : std::string::npos;
    }

    return std::string::npos;
}

bool LLInventoryFilter::isDefault() const
{
    return !isNotDefault();
}

// has user modified default filter params?
bool LLInventoryFilter::isNotDefault() const
{
    S32 not_default = 0;

    not_default |= (mFilterOps.mFilterObjectTypes != mDefaultFilterOps.mFilterObjectTypes);
    not_default |= (mFilterOps.mFilterCategoryTypes != mDefaultFilterOps.mFilterCategoryTypes);
    not_default |= (mFilterOps.mFilterWearableTypes != mDefaultFilterOps.mFilterWearableTypes);
    not_default |= (mFilterOps.mFilterTypes != mDefaultFilterOps.mFilterTypes);
    not_default |= (mFilterOps.mFilterLinks != mDefaultFilterOps.mFilterLinks);
    not_default |= (mFilterSubString.size());
    not_default |= (mFilterOps.mPermissions != mDefaultFilterOps.mPermissions);
    not_default |= (mFilterOps.mMinDate != mDefaultFilterOps.mMinDate);
    not_default |= (mFilterOps.mMaxDate != mDefaultFilterOps.mMaxDate);
    not_default |= (mFilterOps.mHoursAgo != mDefaultFilterOps.mHoursAgo);

    // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
    not_default |= (mFilterOps.mCoalescedObjectsOnly != mDefaultFilterOps.mCoalescedObjectsOnly);

    return not_default != 0;
}

bool LLInventoryFilter::isActive() const
{
    return mFilterOps.mFilterObjectTypes != 0xffffffffffffffffULL
        || mFilterOps.mFilterCategoryTypes != 0xffffffffffffffffULL
        || mFilterOps.mFilterWearableTypes != 0xffffffffffffffffULL
        || mFilterOps.mFilterTypes != FILTERTYPE_OBJECT
        || mFilterOps.mFilterLinks != FILTERLINK_INCLUDE_LINKS
        || mFilterSubString.size()
        || mFilterOps.mPermissions != PERM_NONE
        || mFilterOps.mMinDate != time_min()
        || mFilterOps.mMaxDate != time_max()
        || mFilterOps.mHoursAgo != 0;
}

bool LLInventoryFilter::isModified() const
{
    return mFilterModified != FILTER_NONE;
}

void LLInventoryFilter::updateFilterTypes(U64 types, U64& current_types)
{
    if (current_types != types)
    {
        // keep current items only if no type bits getting turned off
        bool fewer_bits_set = (current_types & ~types) != 0;
        bool more_bits_set = (~current_types & types) != 0;

        current_types = types;
        if (more_bits_set && fewer_bits_set)
        {
            // neither less or more restrictive, both simultaneously
            // so we need to filter from scratch
            setModified(FILTER_RESTART);
        }
        else if (more_bits_set)
        {
            // target is only one of all requested types so more type bits == less restrictive
            setModified(FILTER_LESS_RESTRICTIVE);
        }
        else if (fewer_bits_set)
        {
            setModified(FILTER_MORE_RESTRICTIVE);
        }
    }
}

void LLInventoryFilter::setSearchType(ESearchType type)
{
    if (mSearchType != type)
    {
        mSearchType = type;
        setModified();
    }
}

void LLInventoryFilter::setFilterCreator(EFilterCreatorType type)
{
    if (mFilterOps.mFilterCreatorType != type)
    {
        mFilterOps.mFilterCreatorType = type;
        setModified();
    }
}

void LLInventoryFilter::setFilterObjectTypes(U64 types)
{
    updateFilterTypes(types, mFilterOps.mFilterObjectTypes);
    mFilterOps.mFilterTypes |= FILTERTYPE_OBJECT;
}

// <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
void LLInventoryFilter::setFilterCoalescedObjects(bool coalesced)
{
    mFilterOps.mCoalescedObjectsOnly = coalesced;

    LLInventoryFilter::EFilterModified modifyMode = FILTER_LESS_RESTRICTIVE;

    if (coalesced)
    {
        modifyMode = FILTER_MORE_RESTRICTIVE;
    }

    setModified(modifyMode);
}
// </FS:Zi>

void LLInventoryFilter::setFilterCategoryTypes(U64 types)
{
    updateFilterTypes(types, mFilterOps.mFilterCategoryTypes);
    mFilterOps.mFilterTypes |= FILTERTYPE_CATEGORY;
}

void LLInventoryFilter::setFilterWearableTypes(U64 types)
{
    updateFilterTypes(types, mFilterOps.mFilterWearableTypes);
    mFilterOps.mFilterTypes |= FILTERTYPE_WEARABLE;
}

void LLInventoryFilter::setFilterSettingsTypes(U64 types)
{
    updateFilterTypes(types, mFilterOps.mFilterSettingsTypes);
    mFilterOps.mFilterTypes |= FILTERTYPE_SETTINGS;
}

void LLInventoryFilter::setFilterThumbnails(U64 filter_thumbnails)
{
    if (mFilterOps.mFilterThumbnails != filter_thumbnails)
    {
        if (mFilterOps.mFilterThumbnails == FILTER_EXCLUDE_THUMBNAILS
            && filter_thumbnails == FILTER_ONLY_THUMBNAILS)
        {
            setModified(FILTER_RESTART);
        }
        else if (mFilterOps.mFilterThumbnails == FILTER_ONLY_THUMBNAILS
            && filter_thumbnails == FILTER_EXCLUDE_THUMBNAILS)
        {
            setModified(FILTER_RESTART);
        }
        else if (mFilterOps.mFilterThumbnails == FILTER_INCLUDE_THUMBNAILS)
        {
            setModified(FILTER_MORE_RESTRICTIVE);
        }
        else
        {
            setModified(FILTER_LESS_RESTRICTIVE);
        }
    }
    mFilterOps.mFilterThumbnails = filter_thumbnails;
}

void LLInventoryFilter::setFilterEmptySystemFolders()
{
    mFilterOps.mFilterTypes |= FILTERTYPE_EMPTYFOLDERS;
}

void LLInventoryFilter::setFilterWorn()
{
    mFilterOps.mFilterTypes |= FILTERTYPE_WORN;
}

void LLInventoryFilter::setFilterMarketplaceActiveFolders()
{
    mFilterOps.mFilterTypes |= FILTERTYPE_MARKETPLACE_ACTIVE;
}

void LLInventoryFilter::setFilterMarketplaceInactiveFolders()
{
    mFilterOps.mFilterTypes |= FILTERTYPE_MARKETPLACE_INACTIVE;
}

void LLInventoryFilter::setFilterMarketplaceUnassociatedFolders()
{
    mFilterOps.mFilterTypes |= FILTERTYPE_MARKETPLACE_UNASSOCIATED;
}

void LLInventoryFilter::setFilterMarketplaceListingFolders(bool select_only_listing_folders)
{
    if (select_only_listing_folders)
    {
        mFilterOps.mFilterTypes |= FILTERTYPE_MARKETPLACE_LISTING_FOLDER;
        setModified(FILTER_MORE_RESTRICTIVE);
    }
    else
    {
        mFilterOps.mFilterTypes &= ~FILTERTYPE_MARKETPLACE_LISTING_FOLDER;
        setModified(FILTER_LESS_RESTRICTIVE);
    }
}


void LLInventoryFilter::toggleSearchVisibilityLinks()
{
    bool hide_links = mFilterOps.mSearchVisibility & VISIBILITY_LINKS;
    if (hide_links)
    {
        mFilterOps.mSearchVisibility &= ~VISIBILITY_LINKS;
    }
    else
    {
        mFilterOps.mSearchVisibility |= VISIBILITY_LINKS;
    }

    if (hasFilterString())
    {
        setModified(hide_links ? FILTER_MORE_RESTRICTIVE : FILTER_LESS_RESTRICTIVE);
    }
}

void LLInventoryFilter::toggleSearchVisibilityOutfits()
{
    bool hide_outfits = mFilterOps.mSearchVisibility & VISIBILITY_OUTFITS;
    if (hide_outfits)
    {
        mFilterOps.mSearchVisibility &= ~VISIBILITY_OUTFITS;
    }
    else
    {
        mFilterOps.mSearchVisibility |= VISIBILITY_OUTFITS;
    }

    if (hasFilterString())
    {
        setModified(hide_outfits ? FILTER_MORE_RESTRICTIVE : FILTER_LESS_RESTRICTIVE);
    }
}

void LLInventoryFilter::toggleSearchVisibilityTrash()
{
    bool hide_trash = mFilterOps.mSearchVisibility & VISIBILITY_TRASH;
    if (hide_trash)
    {
        mFilterOps.mSearchVisibility &= ~VISIBILITY_TRASH;
    }
    else
    {
        mFilterOps.mSearchVisibility |= VISIBILITY_TRASH;
    }

    if (hasFilterString())
    {
        setModified(hide_trash ? FILTER_MORE_RESTRICTIVE : FILTER_LESS_RESTRICTIVE);
    }
}

void LLInventoryFilter::toggleSearchVisibilityLibrary()
{
    bool hide_library = mFilterOps.mSearchVisibility & VISIBILITY_LIBRARY;
    if (hide_library)
    {
        mFilterOps.mSearchVisibility &= ~VISIBILITY_LIBRARY;
    }
    else
    {
        mFilterOps.mSearchVisibility |= VISIBILITY_LIBRARY;
    }

    if (hasFilterString())
    {
        setModified(hide_library ? FILTER_MORE_RESTRICTIVE : FILTER_LESS_RESTRICTIVE);
    }
}

void LLInventoryFilter::setFilterNoMarketplaceFolder()
{
    mFilterOps.mFilterTypes |= FILTERTYPE_NO_MARKETPLACE_ITEMS;
}

// <FS:Ansariel> Optional hiding of empty system folders
void LLInventoryFilter::removeFilterEmptySystemFolders()
{
    mFilterOps.mFilterTypes &= ~FILTERTYPE_EMPTYFOLDERS;
}
// </FS:Ansariel> Optional hiding of empty system folders

void LLInventoryFilter::setFilterUUID(const LLUUID& object_id)
{
    if (mFilterOps.mFilterUUID == LLUUID::null)
    {
        setModified(FILTER_MORE_RESTRICTIVE);
    }
    else
    {
        setModified(FILTER_RESTART);
    }

    mFilterOps.mFilterUUID = object_id;
    mFilterOps.mFilterTypes = FILTERTYPE_UUID;
}

void LLInventoryFilter::setFilterSubString(const std::string& string)
{
    std::string filter_sub_string_new = string;
    mFilterSubStringOrig = string;
    LLStringUtil::trimHead(filter_sub_string_new);
    LLStringUtil::toUpper(filter_sub_string_new);

    if (mFilterSubString != filter_sub_string_new)
    {
        mFilterTokens.clear();
        if (filter_sub_string_new.find_first_of("+") != std::string::npos)
        {
            typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
            boost::char_separator<char> sep("+");
            tokenizer tokens(filter_sub_string_new, sep);

            for (auto token_iter : tokens)
            {
                mFilterTokens.push_back(token_iter);
            }
        }

        std::string old_token = mExactToken;
        mExactToken.clear();
        bool exact_token_changed = false;
        if (mFilterTokens.empty() && filter_sub_string_new.size() > 2)
        {
            boost::regex mPattern = boost::regex("\"\\s*([^<]*)?\\s*\"",
                boost::regex::perl | boost::regex::icase);
            boost::match_results<std::string::const_iterator> matches;
            mExactToken = (ll_regex_match(filter_sub_string_new, matches, mPattern) && matches[1].matched)
                ? matches[1]
                : LLStringUtil::null;
            if ((old_token.empty() && !mExactToken.empty())
                || (!old_token.empty() && mExactToken.empty()))
            {
                exact_token_changed = true;
            }
        }

        // hitting BACKSPACE, for example
        const bool less_restrictive = mFilterSubString.size() >= filter_sub_string_new.size()
            && !mFilterSubString.substr(0, filter_sub_string_new.size()).compare(filter_sub_string_new);

        // appending new characters
        const bool more_restrictive = mFilterSubString.size() < filter_sub_string_new.size()
            && !filter_sub_string_new.substr(0, mFilterSubString.size()).compare(mFilterSubString);

        mFilterSubString = filter_sub_string_new;
        if (exact_token_changed)
        {
            setModified(FILTER_RESTART);
        }
        else if (less_restrictive)
        {
            setModified(FILTER_LESS_RESTRICTIVE);
        }
        else if (more_restrictive)
        {
            setModified(FILTER_MORE_RESTRICTIVE);
        }
        else
        {
            setModified(FILTER_RESTART);
        }

        // Cancel out filter links once the search string is modified
        // <FS:Zi> Filter Links Menu
        //if (mFilterOps.mFilterLinks == FILTERLINK_ONLY_LINKS)
        //{
        //  if (mBackupFilterOps.mFilterLinks == FILTERLINK_ONLY_LINKS)
        //  {
        //      // we started viewer/floater in 'only links' mode
        //      mFilterOps.mFilterLinks = FILTERLINK_INCLUDE_LINKS;
        //  }
        //  else
        //  {
        //      mFilterOps = mBackupFilterOps;
        //      setModified(FILTER_RESTART);
        //  }
        //}
        // </FS:Zi>

        // Cancel out UUID once the search string is modified
        if (mFilterOps.mFilterTypes == FILTERTYPE_UUID)
        {
            // <FS:Ansariel> Find all links unhiding hidden empty system folders
            //mFilterOps.mFilterTypes &= ~FILTERTYPE_UUID;
            //mFilterOps.mFilterUUID = LLUUID::null;
            mFilterOps = mBackupFilterOps;
            // </FS:Ansariel>
            setModified(FILTER_RESTART);
        }
    }
}

void LLInventoryFilter::setSearchVisibilityTypes(U32 types)
{
    if (mFilterOps.mSearchVisibility != types)
    {
        // keep current items only if no perm bits getting turned off
        bool fewer_bits_set = (mFilterOps.mSearchVisibility & ~types);
        bool more_bits_set = (~mFilterOps.mSearchVisibility & types);
        mFilterOps.mSearchVisibility = types;

        if (more_bits_set && fewer_bits_set)
        {
            setModified(FILTER_RESTART);
        }
        else if (more_bits_set)
        {
            // target must have all requested permission bits, so more bits == more restrictive
            setModified(FILTER_MORE_RESTRICTIVE);
        }
        else if (fewer_bits_set)
        {
            setModified(FILTER_LESS_RESTRICTIVE);
        }
    }
}

void LLInventoryFilter::setSearchVisibilityTypes(const Params& params)
{
    if (!params.validateBlock())
    {
        return;
    }

    if (params.filter_ops.search_visibility.isProvided())
    {
        setSearchVisibilityTypes(params.filter_ops.search_visibility);
    }
}

void LLInventoryFilter::setFilterPermissions(PermissionMask perms)
{
    if (mFilterOps.mPermissions != perms)
    {
        // keep current items only if no perm bits getting turned off
        bool fewer_bits_set = (mFilterOps.mPermissions & ~perms);
        bool more_bits_set = (~mFilterOps.mPermissions & perms);
        mFilterOps.mPermissions = perms;

        if (more_bits_set && fewer_bits_set)
        {
            setModified(FILTER_RESTART);
        }
        else if (more_bits_set)
        {
            // target must have all requested permission bits, so more bits == more restrictive
            setModified(FILTER_MORE_RESTRICTIVE);
        }
        else if (fewer_bits_set)
        {
            setModified(FILTER_LESS_RESTRICTIVE);
        }
    }
}

void LLInventoryFilter::setDateRange(time_t min_date, time_t max_date)
{
    mFilterOps.mHoursAgo = 0;
    if (mFilterOps.mMinDate != min_date)
    {
        mFilterOps.mMinDate = min_date;
        setModified();
    }
    if (mFilterOps.mMaxDate != llmax(mFilterOps.mMinDate, max_date))
    {
        mFilterOps.mMaxDate = llmax(mFilterOps.mMinDate, max_date);
        setModified();
    }

    if (areDateLimitsSet())
    {
        mFilterOps.mFilterTypes |= FILTERTYPE_DATE;
    }
    else
    {
        mFilterOps.mFilterTypes &= ~FILTERTYPE_DATE;
    }
}

void LLInventoryFilter::setDateRangeLastLogoff(bool sl)
{
    static LLCachedControl<U32> s_last_logoff(gSavedPerAccountSettings, "LastLogoff", 0);
    if (sl && !isSinceLogoff())
    {
        setDateRange(s_last_logoff(), time_max());
        setModified();
    }
    if (!sl && isSinceLogoff())
    {
        setDateRange(time_min(), time_max());
        setModified();
    }

    if (areDateLimitsSet())
    {
        mFilterOps.mFilterTypes |= FILTERTYPE_DATE;
    }
    else
    {
        mFilterOps.mFilterTypes &= ~FILTERTYPE_DATE;
    }
}

bool LLInventoryFilter::isSinceLogoff() const
{
    static LLCachedControl<U32> s_last_logoff(gSavedSettings, "LastLogoff", 0);

    return (mFilterOps.mMinDate == (time_t)s_last_logoff()) &&
        (mFilterOps.mMaxDate == time_max()) &&
        (mFilterOps.mFilterTypes & FILTERTYPE_DATE);
}

void LLInventoryFilter::clearModified()
{
    mFilterModified = FILTER_NONE;
}

void LLInventoryFilter::setHoursAgo(U32 hours)
{
    if (mFilterOps.mHoursAgo != hours)
    {
        bool are_date_limits_valid = mFilterOps.mMinDate == time_min() && mFilterOps.mMaxDate == time_max();

        bool is_increasing = hours > mFilterOps.mHoursAgo;
        bool is_decreasing = hours < mFilterOps.mHoursAgo;
        bool is_increasing_from_zero = is_increasing && !mFilterOps.mHoursAgo && !isSinceLogoff();

        // *NOTE: need to cache last filter time, in case filter goes stale
        bool less_restrictive;
        bool more_restrictive;
        if (FILTERDATEDIRECTION_NEWER == mFilterOps.mDateSearchDirection)
        {
            less_restrictive = ((are_date_limits_valid && ((is_increasing && mFilterOps.mHoursAgo))) || !hours);
            more_restrictive = ((are_date_limits_valid && (!is_increasing && hours)) || is_increasing_from_zero);
        }
        else
        {
            less_restrictive = ((are_date_limits_valid && ((is_decreasing && mFilterOps.mHoursAgo))) || !hours);
            more_restrictive = ((are_date_limits_valid && (!is_decreasing && hours)) || is_increasing_from_zero);
        }

        mFilterOps.mHoursAgo = hours;
        mFilterOps.mMinDate = time_min();
        mFilterOps.mMaxDate = time_max();
        if (less_restrictive)
        {
            setModified(FILTER_LESS_RESTRICTIVE);
        }
        else if (more_restrictive)
        {
            setModified(FILTER_MORE_RESTRICTIVE);
        }
        else
        {
            setModified(FILTER_RESTART);
        }
    }

    if (areDateLimitsSet())
    {
        mFilterOps.mFilterTypes |= FILTERTYPE_DATE;
    }
    else
    {
        mFilterOps.mFilterTypes &= ~FILTERTYPE_DATE;
    }
}

void LLInventoryFilter::setDateSearchDirection(U32 direction)
{
    if (direction != mFilterOps.mDateSearchDirection)
    {
        mFilterOps.mDateSearchDirection = direction;
        setModified(FILTER_RESTART);
    }
}

U32 LLInventoryFilter::getDateSearchDirection() const
{
    return mFilterOps.mDateSearchDirection;
}

void LLInventoryFilter::setFilterLinks(U64 filter_links)
{
    // <FS:Zi> Filter Links Menu
    // if (mFilterOps.mFilterLinks != filter_links)
    // {
    //  if (mFilterOps.mFilterLinks == FILTERLINK_EXCLUDE_LINKS ||
    //      mFilterOps.mFilterLinks == FILTERLINK_ONLY_LINKS)
    //      setModified(FILTER_MORE_RESTRICTIVE);
    //  else
    //      setModified(FILTER_LESS_RESTRICTIVE);
    // }
    // mFilterOps.mFilterLinks = filter_links;
    if (mFilterOps.mFilterLinks != filter_links)
    {
        LLInventoryFilter::EFilterModified modifyMode = FILTER_RESTART;

        if (filter_links == FILTERLINK_INCLUDE_LINKS)
        {
            modifyMode = FILTER_LESS_RESTRICTIVE;
        }
        else if (mFilterOps.mFilterLinks == FILTERLINK_INCLUDE_LINKS)
        {
            modifyMode = FILTER_MORE_RESTRICTIVE;
        }
        else if (filter_links == FILTERLINK_EXCLUDE_LINKS && mFilterOps.mFilterLinks == FILTERLINK_INCLUDE_LINKS)
        {
            modifyMode = FILTER_MORE_RESTRICTIVE;
        }
        else if (filter_links == FILTERLINK_ONLY_LINKS && mFilterOps.mFilterLinks == FILTERLINK_INCLUDE_LINKS)
        {
            modifyMode = FILTER_MORE_RESTRICTIVE;
        }

        mFilterOps.mFilterLinks = filter_links;
        setModified(modifyMode);
    }
    // </FS:Zi>
}

void LLInventoryFilter::setShowFolderState(EFolderShow state)
{
    if (mFilterOps.mShowFolderState != state)
    {
        mFilterOps.mShowFolderState = state;
        if (state == SHOW_NON_EMPTY_FOLDERS)
        {
            // showing fewer folders than before
            setModified(FILTER_MORE_RESTRICTIVE);
        }
        else if (state == SHOW_ALL_FOLDERS)
        {
            // showing same folders as before and then some
            setModified(FILTER_LESS_RESTRICTIVE);
        }
        else
        {
            setModified();
        }
    }
}

void LLInventoryFilter::setFindAllLinksMode(const std::string &search_name, const LLUUID& search_id)
{
    // Save a copy of settings so that we will be able to restore it later
    // but make sure we are not searching for links already
    //if (mFilterOps.mFilterLinks != FILTERLINK_ONLY_LINKS) // <FS:Ansariel> Find all links unhiding hidden empty system folders
    {
        mBackupFilterOps = mFilterOps;
    }

    // set search options
    setFilterSubString(search_name);
    setFilterUUID(search_id);
    setShowFolderState(SHOW_NON_EMPTY_FOLDERS);
    setFilterLinks(FILTERLINK_ONLY_LINKS);
}

void LLInventoryFilter::markDefault()
{
    mDefaultFilterOps = mFilterOps;
}

void LLInventoryFilter::resetDefault()
{
    mFilterOps = mDefaultFilterOps;
    setModified();
}

void LLInventoryFilter::setModified(EFilterModified behavior)
{
    mFilterText.clear();
    mCurrentGeneration++;

    if (mFilterModified == FILTER_NONE)
    {
        mFilterModified = behavior;
    }
    else if (mFilterModified != behavior)
    {
        // trying to do both less restrictive and more restrictive filter
        // basically means restart from scratch
        mFilterModified = FILTER_RESTART;
    }

    // if not keeping current filter results, update last valid as well
    switch (mFilterModified)
    {
        case FILTER_RESTART:
            mFirstRequiredGeneration = mCurrentGeneration;
            mFirstSuccessGeneration = mCurrentGeneration;
            break;
        case FILTER_LESS_RESTRICTIVE:
            mFirstRequiredGeneration = mCurrentGeneration;
            break;
        case FILTER_MORE_RESTRICTIVE:
            mFirstSuccessGeneration = mCurrentGeneration;
            break;
        default:
            LL_ERRS() << "Bad filter behavior specified" << LL_ENDL;
    }
}

bool LLInventoryFilter::isFilterObjectTypesWith(LLInventoryType::EType t) const
{
    return mFilterOps.mFilterObjectTypes & (1LL << t);
}

const std::string& LLInventoryFilter::getFilterText()
{
    if (!mFilterText.empty())
    {
        return mFilterText;
    }

    std::string filtered_types;
    std::string not_filtered_types;
    bool filtered_by_type = false;
    bool filtered_by_all_types = true;
    S32 num_filter_types = 0;

    mFilterText.clear();

    if (isFilterObjectTypesWith(LLInventoryType::IT_ANIMATION))
    {
        filtered_types += LLTrans::getString("Animations");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types += LLTrans::getString("Animations");

        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_CALLINGCARD))
    {
        filtered_types += LLTrans::getString("Calling Cards");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types += LLTrans::getString("Calling Cards");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_WEARABLE))
    {
        filtered_types +=  LLTrans::getString("Clothing");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Clothing");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_GESTURE))
    {
        filtered_types +=  LLTrans::getString("Gestures");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Gestures");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_LANDMARK))
    {
        filtered_types +=  LLTrans::getString("Landmarks");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Landmarks");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_MATERIAL))
    {
        filtered_types +=  LLTrans::getString("Materials");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Materials");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_NOTECARD))
    {
        filtered_types +=  LLTrans::getString("Notecards");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Notecards");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_OBJECT) && isFilterObjectTypesWith(LLInventoryType::IT_ATTACHMENT))
    {
        filtered_types +=  LLTrans::getString("Objects");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Objects");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_LSL))
    {
        filtered_types +=  LLTrans::getString("Scripts");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Scripts");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_SOUND))
    {
        filtered_types +=  LLTrans::getString("Sounds");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Sounds");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_TEXTURE))
    {
        filtered_types +=  LLTrans::getString("Textures");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Textures");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_SNAPSHOT))
    {
        filtered_types +=  LLTrans::getString("Snapshots");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Snapshots");
        filtered_by_all_types = false;
    }

    if (isFilterObjectTypesWith(LLInventoryType::IT_SETTINGS))
    {
        filtered_types +=  LLTrans::getString("Settings");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Settings");
        filtered_by_all_types = false;
    }

// <AS:chanayane> Search folders only
    if (isFilterObjectTypesWith(LLInventoryType::IT_CATEGORY))
    {
        filtered_types +=  LLTrans::getString("Folders");
        filtered_by_type = true;
        num_filter_types++;
    }
    else
    {
        not_filtered_types +=  LLTrans::getString("Folders");
        filtered_by_all_types = false;
    }
// </AS:chanayane>

    if (!LLInventoryModelBackgroundFetch::instance().folderFetchActive()
        && filtered_by_type
        && !filtered_by_all_types)
    {
        mFilterText += " - ";
        if (num_filter_types < 5)
        {
            mFilterText += filtered_types;
        }
        else
        {
            mFilterText += LLTrans::getString("No Filters");
            mFilterText += not_filtered_types;
        }
        // remove the ',' at the end
        mFilterText.erase(mFilterText.size() - 1, 1);
    }

    // <FS:Ansariel> FIRE-19340: search inventory by transferable permission
    //           Zi: FIRE-1175 - Filter Permissions Menu
    PermissionMask permissions = getFilterPermissions();
    if (permissions)
    {
        std::string perm_string;
        if (permissions & PERM_MODIFY)
        {
            perm_string = LLTrans::getString("Modifiable");
        }

        if (permissions & PERM_COPY)
        {
            if (!perm_string.empty())
            {
                perm_string += ", ";
            }
            perm_string += LLTrans::getString("Copyable");
        }

        if (permissions & PERM_TRANSFER)
        {
            if (!perm_string.empty())
            {
                perm_string += ", ";
            }
            perm_string += LLTrans::getString("Transferable");
        }

        LLStringUtil::format_map_t args;
        args["[PERMISSIONS]"] = perm_string;

        mFilterText += LLTrans::getString("PermissionsFilter", args);
    }
    // </FS:Ansariel>

    if (isSinceLogoff())
    {
        mFilterText += LLTrans::getString("Since Logoff");
    }

    return mFilterText;
}

LLInventoryFilter& LLInventoryFilter::operator =(const LLInventoryFilter& other)
{
    setFilterObjectTypes(other.getFilterObjectTypes());
    setDateRange(other.getMinDate(), other.getMaxDate());
    setHoursAgo(other.getHoursAgo());
    setDateSearchDirection(other.getDateSearchDirection());
    setShowFolderState(other.getShowFolderState());
    setFilterPermissions(other.getFilterPermissions());
    setFilterSubString(other.getFilterSubString());
    setDateRangeLastLogoff(other.isSinceLogoff());
    // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
    setFilterCoalescedObjects(other.getFilterCoalescedObjects());
    return *this;
}

void LLInventoryFilter::toParams(Params& params) const
{
    params.filter_ops.types = (U32)getFilterObjectTypes();
    params.filter_ops.category_types = getFilterCategoryTypes();
    if (getFilterObjectTypes() & FILTERTYPE_WEARABLE)
    {
        params.filter_ops.wearable_types = getFilterWearableTypes();
    }
    params.filter_ops.date_range.min_date = getMinDate();
    params.filter_ops.date_range.max_date = getMaxDate();
    params.filter_ops.hours_ago = getHoursAgo();
    params.filter_ops.date_search_direction = getDateSearchDirection();
    params.filter_ops.show_folder_state = getShowFolderState();
    params.filter_ops.creator_type = getFilterCreatorType();
    params.filter_ops.permissions = getFilterPermissions();
    params.filter_ops.search_visibility = (U32)getSearchVisibilityTypes();
    params.substring = getFilterSubString();
    params.since_logoff = isSinceLogoff();
    // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
    params.filter_ops.coalesced_objects_only = getFilterCoalescedObjects();
}

void LLInventoryFilter::fromParams(const Params& params)
{
    if (!params.validateBlock())
    {
        return;
    }

    // <FS:Ansariel> FIRE-12418: Only apply filter params if they are really provided
    //setFilterObjectTypes(params.filter_ops.types);
    //setFilterCategoryTypes(params.filter_ops.category_types);
    //setFilterWearableTypes(params.filter_ops.wearable_types);
    //setDateRange(params.filter_ops.date_range.min_date,   params.filter_ops.date_range.max_date);
    //setHoursAgo(params.filter_ops.hours_ago);
    //setDateSearchDirection(params.filter_ops.date_search_direction);
    //setFilterCreator(params.filter_ops.creator_type);
    //setShowFolderState(params.filter_ops.show_folder_state);
    //setFilterPermissions(params.filter_ops.permissions);
    //setSearchVisibilityTypes(params.filter_ops.search_visibility);
    //setFilterSubString(params.substring);
    //setDateRangeLastLogoff(params.since_logoff);
    if (params.filter_ops.types.isProvided())
    {
        setFilterObjectTypes(params.filter_ops.types);
    }
    if (params.filter_ops.category_types.isProvided())
    {
        setFilterCategoryTypes(params.filter_ops.category_types);
    }
    if (params.filter_ops.wearable_types.isProvided())
    {
        setFilterWearableTypes(params.filter_ops.wearable_types);
    }
    if (params.filter_ops.date_range.min_date.isProvided() && params.filter_ops.date_range.max_date.isProvided())
    {
        setDateRange(params.filter_ops.date_range.min_date,   params.filter_ops.date_range.max_date);
    }
    if (params.filter_ops.hours_ago.isProvided())
    {
        setHoursAgo(params.filter_ops.hours_ago);
    }
    if (params.filter_ops.date_search_direction.isProvided())
    {
        setDateSearchDirection(params.filter_ops.date_search_direction);
    }
    if (params.filter_ops.show_folder_state.isProvided())
    {
        setShowFolderState(params.filter_ops.show_folder_state);
    }
    if (params.filter_ops.creator_type.isProvided())
    {
        setFilterCreator(params.filter_ops.creator_type);
    }
    if (params.filter_ops.permissions.isProvided())
    {
        setFilterPermissions(params.filter_ops.permissions);
    }
    if (params.filter_ops.search_visibility.isProvided())
    {
        setSearchVisibilityTypes(params.filter_ops.search_visibility);
    }
    // <FS:Ansariel> FIRE-8947: Don't restore filter string on relog
    //if (params.substring.isProvided())
    //{
    //  setFilterSubString(params.substring);
    //}
    if (params.since_logoff.isProvided())
    {
        setDateRangeLastLogoff(params.since_logoff);
    }
    // </FS:Ansariel>

    // <FS:Zi> FIRE-31369: Add inventory filter for coalesced objects
    if (params.filter_ops.coalesced_objects_only.isProvided())
    {
        setFilterCoalescedObjects(params.filter_ops.coalesced_objects_only);
    }
    // </FS:Zi>
}

U64 LLInventoryFilter::getFilterTypes() const
{
    return mFilterOps.mFilterTypes;
}

U64 LLInventoryFilter::getFilterObjectTypes() const
{
    return mFilterOps.mFilterObjectTypes;
}

U64 LLInventoryFilter::getFilterCategoryTypes() const
{
    return mFilterOps.mFilterCategoryTypes;
}

U64 LLInventoryFilter::getFilterWearableTypes() const
{
    return mFilterOps.mFilterWearableTypes;
}

U64 LLInventoryFilter::getFilterSettingsTypes() const
{
    return mFilterOps.mFilterSettingsTypes;
}

U64 LLInventoryFilter::getSearchVisibilityTypes() const
{
    return mFilterOps.mSearchVisibility;
}

U64 LLInventoryFilter::getFilterThumbnails() const
{
    return mFilterOps.mFilterThumbnails;
}

bool LLInventoryFilter::hasFilterString() const
{
    return mFilterSubString.size() > 0;
}

std::string::size_type LLInventoryFilter::getFilterStringSize() const
{
    return mFilterSubString.size();
}

PermissionMask LLInventoryFilter::getFilterPermissions() const
{
    return mFilterOps.mPermissions;
}

time_t LLInventoryFilter::getMinDate() const
{
    return mFilterOps.mMinDate;
}

time_t LLInventoryFilter::getMaxDate() const
{
    return mFilterOps.mMaxDate;
}
U32 LLInventoryFilter::getHoursAgo() const
{
    return mFilterOps.mHoursAgo;
}
U64 LLInventoryFilter::getFilterLinks() const
{
    return mFilterOps.mFilterLinks;
}
LLInventoryFilter::EFolderShow LLInventoryFilter::getShowFolderState() const
{
    return mFilterOps.mShowFolderState;
}

LLInventoryFilter::EFilterCreatorType LLInventoryFilter::getFilterCreatorType() const
{
    return mFilterOps.mFilterCreatorType;
}

bool LLInventoryFilter::isTimedOut()
{
    return mFilterTime.hasExpired();
}

void LLInventoryFilter::resetTime(S32 timeout)
{
    mFilterTime.reset();
    F32 time_in_sec = (F32)(timeout)/1000.0f;
    mFilterTime.setTimerExpirySec(time_in_sec);
}

S32 LLInventoryFilter::getCurrentGeneration() const
{
    return mCurrentGeneration;
}
S32 LLInventoryFilter::getFirstSuccessGeneration() const
{
    return mFirstSuccessGeneration;
}
S32 LLInventoryFilter::getFirstRequiredGeneration() const
{
    return mFirstRequiredGeneration;
}

void LLInventoryFilter::setEmptyLookupMessage(const std::string& message)
{
    mEmptyLookupMessage = message;
}

void LLInventoryFilter::setDefaultEmptyLookupMessage(const std::string& message)
{
    mDefaultEmptyLookupMessage = message;
}

std::string LLInventoryFilter::getEmptyLookupMessage(bool is_empty_folder) const
{
    if ((isDefault() || is_empty_folder) && !mDefaultEmptyLookupMessage.empty())
    {
        return LLTrans::getString(mDefaultEmptyLookupMessage);
    }
    else
    {
        LLStringUtil::format_map_t args;
        args["[SEARCH_TERM]"] = LLURI::escape(getFilterSubStringOrig());

        return LLTrans::getString(mEmptyLookupMessage, args);
    }
}

bool LLInventoryFilter::areDateLimitsSet()
{
    return mFilterOps.mMinDate != time_min()
        || mFilterOps.mMaxDate != time_max()
        || mFilterOps.mHoursAgo != 0;
}

bool LLInventoryFilter::showAllResults() const
{
    return hasFilterString() && !mSingleFolderMode;
}

bool LLInventoryFilter::FilterOps::DateRange::validateBlock( bool   emit_errors /*= true*/ ) const
{
    bool valid = LLInitParam::Block<DateRange>::validateBlock(emit_errors);
    if (valid)
    {
        if (max_date() < min_date())
        {
            if (emit_errors)
            {
                LL_WARNS() << "max_date should be greater or equal to min_date" <<   LL_ENDL;
            }
            valid = false;
        }
    }
    return valid;
}
