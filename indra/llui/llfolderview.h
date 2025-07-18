/**
 * @file llfolderview.h
 * @brief Definition of the folder view collection of classes.
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

/**
 *
 * The folder view collection of classes provides an interface for
 * making a 'folder view' similar to the way the a single pane file
 * folder interface works.
 *
 */

#ifndef LL_LLFOLDERVIEW_H
#define LL_LLFOLDERVIEW_H

#include "llfolderviewitem.h"   // because LLFolderView is-a LLFolderViewFolder

#include "lluictrl.h"
#include "v4color.h"
#include "lldepthstack.h"
#include "lleditmenuhandler.h"
#include "llfontgl.h"
#include "llscrollcontainer.h"

class LLFolderViewModelInterface;
class LLFolderViewGroupedItemModel;
class LLFolderViewFolder;
class LLFolderViewItem;
class LLFolderViewFilter;
class LLPanel;
class LLLineEditor;
class LLMenuGL;
class LLUICtrl;
class LLTextBox;

/**
 * Class LLFolderViewScrollContainer
 *
 * A scroll container which provides the information about the height
 * of currently displayed folder view contents.
 * Used for updating vertical scroll bar visibility in inventory panel.
 * See LLScrollContainer::calcVisibleSize().
 */
class LLFolderViewScrollContainer : public LLScrollContainer
{
public:
    /*virtual*/ ~LLFolderViewScrollContainer() {};
    /*virtual*/ const LLRect getScrolledViewRect() const;

protected:
    LLFolderViewScrollContainer(const LLScrollContainer::Params& p);
    friend class LLUICtrlFactory;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFolderView
//
// The LLFolderView represents the root level folder view object.
// It manages the screen region of the folder view.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLFolderView : public LLFolderViewFolder, public LLEditMenuHandler
{
public:
    struct Params : public LLInitParam::Block<Params, LLFolderViewFolder::Params>
    {
        Mandatory<LLPanel*>     parent_panel;
        Optional<std::string>   title;
        Optional<bool>          use_label_suffix,
                                allow_multiselect,
                                allow_drag,
                                show_empty_message,
                                use_ellipses,
                                show_item_link_overlays,
                                suppress_folder_menu;
        Mandatory<LLFolderViewModelInterface*>  view_model;
        Optional<LLFolderViewGroupedItemModel*> grouped_item_model;
        Mandatory<std::string>   options_menu;


        Params();
    };

    friend class LLFolderViewScrollContainer;
    typedef folder_view_item_deque selected_items_t;

    LLFolderView(const Params&);
    virtual ~LLFolderView( void );

    virtual bool canFocusChildren() const;

    virtual const LLFolderView* getRoot() const { return this; }
    virtual LLFolderView*   getRoot() { return this; }

    LLFolderViewModelInterface* getFolderViewModel() { return mViewModel; }
    const LLFolderViewModelInterface* getFolderViewModel() const { return mViewModel; }

    LLFolderViewGroupedItemModel* getFolderViewGroupedItemModel() { return mGroupedItemModel; }
    const LLFolderViewGroupedItemModel* getFolderViewGroupedItemModel() const { return mGroupedItemModel; }

    typedef boost::signals2::signal<void (const std::deque<LLFolderViewItem*>& items, bool user_action)> signal_t;
    void setSelectCallback(const signal_t::slot_type& cb) { mSelectSignal.connect(cb); }
    void setReshapeCallback(const signal_t::slot_type& cb) { mReshapeSignal.connect(cb); }

    bool getAllowMultiSelect() const { return mAllowMultiSelect; }
    bool getAllowDrag() const { return mAllowDrag; }

    void setSingleFolderMode(bool is_single_mode) { mSingleFolderMode = is_single_mode; }
    bool isSingleFolderMode() const { return mSingleFolderMode; }

    // Close all folders in the view
    void closeAllFolders();
    void openTopLevelFolders();

    virtual void addFolder( LLFolderViewFolder* folder);

    // Find width and height of this object and its children. Also
    // makes sure that this view and its children are the right size.
    virtual S32 arrange( S32* width, S32* height );
    virtual S32 getItemHeight() const;

    void arrangeAll() { mArrangeGeneration++; }
    S32 getArrangeGeneration() const { return mArrangeGeneration; }

    // applies filters to control visibility of items
    virtual void filter( LLFolderViewFilter& filter);

    void              clearHoveredItem() { setHoveredItem(nullptr); }
    LLFolderViewItem* getHoveredItem() const;
    void              setHoveredItem(LLFolderViewItem* itemp);

    // Get the last selected item
    virtual LLFolderViewItem* getCurSelectedItem( void );
    selected_items_t& getSelectedItems( void );

    // Record the selected item and pass it down the hierarchy.
    virtual bool setSelection(LLFolderViewItem* selection, bool openitem,
        bool take_keyboard_focus = true);

    // This method is used to toggle the selection of an item. Walks
    // children, and keeps track of selected objects.
    virtual bool changeSelection(LLFolderViewItem* selection, bool selected);

    virtual std::set<LLFolderViewItem*> getSelectionList() const;

    // Make sure if ancestor is selected, descendants are not
    void sanitizeSelection();
    virtual void clearSelection();
    void addToSelectionList(LLFolderViewItem* item);
    void removeFromSelectionList(LLFolderViewItem* item);

// [SL:KB] - Patch: Inventory-DragDrop | Checked: 2014-02-04 (Catznip-3.6)
    void setDragStart(S32 screen_x, S32 screen_y);
    bool isOverDragThreshold(S32 screen_x, S32 screen_y);
// [/SL:KB]
    bool startDrag();
    void setDragAndDropThisFrame() { mDragAndDropThisFrame = true; }
    void setDraggingOverItem(LLFolderViewItem* item) { mDraggingOverItem = item; }
    LLFolderViewItem* getDraggingOverItem() { return mDraggingOverItem; }

    // Deletion functionality
    void removeSelectedItems();

    void autoOpenItem(LLFolderViewFolder* item);
    void closeAutoOpenedFolders();
    bool autoOpenTest(LLFolderViewFolder* item);
    bool isOpen() const { return true; } // root folder always open

    // Copy & paste
    virtual bool    canCopy() const;
    virtual void    copy();

    virtual bool    canCut() const;
    virtual void    cut();

    virtual bool    canPaste() const;
    virtual void    paste();

    LLFolderViewItem* getNextUnselectedItem();

    // Public rename functionality - can only start the process
    void startRenamingSelectedItem( void );

    // LLView functionality
    ///*virtual*/ bool handleKey( KEY key, MASK mask, bool called_from_parent );
    /*virtual*/ bool handleKeyHere( KEY key, MASK mask );
    /*virtual*/ bool handleUnicodeCharHere(llwchar uni_char);
    /*virtual*/ bool handleMouseDown( S32 x, S32 y, MASK mask );
    /*virtual*/ bool handleDoubleClick( S32 x, S32 y, MASK mask );
    /*virtual*/ bool handleRightMouseDown( S32 x, S32 y, MASK mask );
    /*virtual*/ bool handleScrollWheel(S32 x, S32 y, S32 clicks); // <FS:Ansariel> FIRE-14223: Using mouse scroll while renaming an item moves line editor out of inventory floater
    /*virtual*/ bool handleHover( S32 x, S32 y, MASK mask );
    /*virtual*/ bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                                   EDragAndDropType cargo_type,
                                   void* cargo_data,
                                   EAcceptance* accept,
                                   std::string& tooltip_msg);
    /*virtual*/ void reshape(S32 width, S32 height, bool called_from_parent = true);
    /*virtual*/ void onMouseLeave(S32 x, S32 y, MASK mask) { setShowSelectionContext(false); }
    virtual void draw();
    virtual void deleteAllChildren();

    void stopAutoScollining() {mNeedsScroll = false;}
    void scrollToShowSelection();
    void scrollToShowItem(LLFolderViewItem* item, const LLRect& constraint_rect);
    void setScrollContainer( LLScrollContainer* parent ) { mScrollContainer = parent; }
    LLRect getVisibleRect();

    bool search(LLFolderViewItem* first_item, const std::string &search_string, bool backward);
    void setShowSelectionContext(bool show) { mShowSelectionContext = show; }
    bool getShowSelectionContext();
    void setShowSingleSelection(bool show);
    bool getShowSingleSelection() const { return mShowSingleSelection; }
    F32  getSelectionFadeElapsedTime() const { return mMultiSelectionFadeTimer.getElapsedTimeF32(); }
    bool getUseEllipses() const { return mUseEllipses; }
    S32 getSelectedCount() const { return (S32)mSelectedItems.size(); }

    void update(); // needs to be called periodically (e.g. once per frame)

    bool needsAutoSelect() const { return mNeedsAutoSelect && !mAutoSelectOverride; }
    bool needsAutoRename() const { return mNeedsAutoRename; }
    void setNeedsAutoRename(bool val) { mNeedsAutoRename = val; }
    void setPinningSelectedItem(bool val) { mPinningSelectedItem = val; }
    void setAutoSelectOverride(bool val) { mAutoSelectOverride = val; }

    bool showItemLinkOverlays() const { return mShowItemLinkOverlays; }

    void setCallbackRegistrar(LLUICtrl::CommitCallbackRegistry::ScopedRegistrar* registrar) { mCallbackRegistrar = registrar; }
    void setEnableRegistrar(LLUICtrl::EnableCallbackRegistry::ScopedRegistrar* registrar) { mEnableRegistrar = registrar; }

    void setForceArrange(bool force) { mForceArrange = force; }

    LLPanel* getParentPanel() const { return mParentPanel.get(); }
    // DEBUG only
    void dumpSelectionInformation();

    virtual S32 notify(const LLSD& info) ;

    void setShowEmptyMessage(bool show_msg) { mShowEmptyMessage = show_msg; }

    bool useLabelSuffix() const { return mUseLabelSuffix; }
    virtual void updateMenu();

    void finishRenamingItem( void );

    // Note: We may eventually have to move that method up the hierarchy to LLFolderViewItem.
    LLHandle<LLFolderView>  getHandle() const { return getDerivedHandle<LLFolderView>(); }

private:
    void updateMenuOptions(LLMenuGL* menu);
    void updateRenamerPosition();
    static void onIdleUpdateMenu(void* user_data);

protected:
    LLScrollContainer* mScrollContainer;  // NULL if this is not a child of a scroll container.

    void commitRename( const LLSD& data );
    void onRenamerLost();

    void closeRenamer( void );

    bool isFolderSelected();

    bool selectFirstItem();
    bool selectLastItem();

    bool addNoOptions(LLMenuGL* menu) const;


protected:
    LLHandle<LLView>                    mPopupMenuHandle;
    std::string                     mMenuFileName;

    LLHandle<LLView>                mHoveredItem;
    selected_items_t                mSelectedItems;
    bool                            mKeyboardSelection,
                                    mAllowMultiSelect,
                                    mAllowDrag,
                                    mShowEmptyMessage,
                                    mShowFolderHierarchy,
                                    mNeedsScroll,
                                    mPinningSelectedItem,
                                    mNeedsAutoSelect,
                                    mAutoSelectOverride,
                                    mNeedsAutoRename,
                                    mUseLabelSuffix,
                                    mDragAndDropThisFrame,
                                    mShowItemLinkOverlays,
                                    mShowSelectionContext,
                                    mShowSingleSelection,
                                    mSuppressFolderMenu,
                                    mSingleFolderMode;

    // Renaming variables and methods
    LLFolderViewItem*               mRenameItem;  // The item currently being renamed
    LLLineEditor*                   mRenamer;

    LLRect                          mScrollConstraintRect;

    LLDepthStack<LLFolderViewFolder>    mAutoOpenItems;
    LLFolderViewFolder*             mAutoOpenCandidate;
    LLFrameTimer                    mAutoOpenTimer;
    LLFrameTimer                    mSearchTimer;
    std::string                     mSearchString;
    LLFrameTimer                    mMultiSelectionFadeTimer;
    S32                             mArrangeGeneration;

    signal_t                        mSelectSignal;
    signal_t                        mReshapeSignal;
    S32                             mSignalSelectCallback;
    S32                             mMinWidth;
// [SL:KB] - Patch: Inventory-DragDrop | Checked: 2014-02-04 (Catznip-3.6)
    S32                             mDragStartX;
    S32                             mDragStartY;
// [/SL:KB]

    LLHandle<LLPanel>               mParentPanel;

    LLFolderViewModelInterface*     mViewModel;
    LLFolderViewGroupedItemModel*   mGroupedItemModel;

    /**
     * Is used to determine if we need to cut text In LLFolderViewItem to avoid horizontal scroll.
     * NOTE: For now it's used only to cut LLFolderViewItem::mLabel text for Landmarks in Places Panel.
     */
    bool                            mUseEllipses; // See EXT-719

    /**
     * Contains item under mouse pointer while dragging
     */
    LLFolderViewItem*               mDraggingOverItem; // See EXT-719

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar* mCallbackRegistrar;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar* mEnableRegistrar;

    boost::signals2::connection mRenamerTopLostSignalConnection;

    bool mForceArrange;

public:
    static F32 sAutoOpenTime;
    LLTextBox*                      mStatusTextBox;

};


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFolderViewFunctor
//
// Simple abstract base class for applying a functor to folders and
// items in a folder view hierarchy. This is suboptimal for algorithms
// that only work folders or only work on items, but I'll worry about
// that later when it's determined to be too slow.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLFolderViewFunctor
{
public:
    virtual ~LLFolderViewFunctor() {}
    virtual void doFolder(LLFolderViewFolder* folder) = 0;
    virtual void doItem(LLFolderViewItem* item) = 0;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLSelectFirstFilteredItem
//
// This will select the first *item* found in the hierarchy. If no item can be
// selected, the first matching folder will.
// Since doFolder() is done first but we prioritize item selection, we let the
// first filtered folder set the selection and raise a folder flag.
// The selection might be overridden by the first filtered item in doItem()
// which checks an item flag. Since doFolder() checks the item flag too, the first
// item will still be selected if items were to be done first and folders second.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLSelectFirstFilteredItem : public LLFolderViewFunctor
{
public:
    LLSelectFirstFilteredItem() : mItemSelected(false), mFolderSelected(false) {}
    virtual ~LLSelectFirstFilteredItem() {}
    virtual void doFolder(LLFolderViewFolder* folder);
    virtual void doItem(LLFolderViewItem* item);
    bool wasItemSelected() const { return mItemSelected || mFolderSelected; }
protected:
    bool mItemSelected;
    bool mFolderSelected;
};

class LLOpenFilteredFolders : public LLFolderViewFunctor
{
public:
    LLOpenFilteredFolders()  {}
    virtual ~LLOpenFilteredFolders() {}
    virtual void doFolder(LLFolderViewFolder* folder);
    virtual void doItem(LLFolderViewItem* item);
};

class LLSaveFolderState : public LLFolderViewFunctor
{
public:
    LLSaveFolderState() : mApply(false) {}
    virtual ~LLSaveFolderState() {}
    virtual void doFolder(LLFolderViewFolder* folder);
    virtual void doItem(LLFolderViewItem* item) {}
    void setApply(bool apply);
    void clearOpenFolders() { mOpenFolders.clear(); }
    bool hasOpenFolders() { return !mOpenFolders.empty(); }
protected:
    std::set<LLUUID> mOpenFolders;
    bool mApply;
};

class LLOpenFoldersWithSelection : public LLFolderViewFunctor
{
public:
    LLOpenFoldersWithSelection() {}
    virtual ~LLOpenFoldersWithSelection() {}
    virtual void doFolder(LLFolderViewFolder* folder);
    virtual void doItem(LLFolderViewItem* item);
};

class LLAllDescendentsPassedFilter : public LLFolderViewFunctor
{
public:
    LLAllDescendentsPassedFilter() : mAllDescendentsPassedFilter(true) {}
    /*virtual*/ ~LLAllDescendentsPassedFilter() {}
    /*virtual*/ void doFolder(LLFolderViewFolder* folder);
    /*virtual*/ void doItem(LLFolderViewItem* item);
    bool allDescendentsPassedFilter() const { return mAllDescendentsPassedFilter; }
protected:
    bool mAllDescendentsPassedFilter;
};

// Flags for buildContextMenu()
const U32 SUPPRESS_OPEN_ITEM = 0x1;
const U32 FIRST_SELECTED_ITEM = 0x2;
const U32 ITEM_IN_MULTI_SELECTION = 0x4;

#endif // LL_LLFOLDERVIEW_H
