/** 
 * @file shtextureinspector.cpp
 * @brief Texture inspection tool
 * @Author Shyotl Kuhr
 *
 * $LicenseInfo:firstyear=2010&license=viewergpl$
 * 
 * Copyright (c) 2010, Shyotl Kuhr.
 * 
 * ALL SOURCE CODE IS PROVIDED "AS IS." THE CREATOR MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 */


/*
Simple panel that lists textures used on selected objects and displays available metadata

Currently displays thumbnails of textures used on selected objects. Does not show dupes.
Shows encoded upload info metadata if available, and displays folder icon overlaying texture if found in inventory.
- TODO: Visually disable changing of texture in texture preview submenu(s). (currently only blocks on final apply)
- TODO: Denote textures with metadata via displaying something on thumbnail. Asterisk? Perhaps something in the label?
- TODO: Use a custom selection tool. Opening the edit window suffices for now.
- UNDECIDED: Pull upload info from inv item if found? Creator of inv item can differ from true uploader of texture, however.
	If such is added, it should only be fallback if no metadata is found, and should be signified as such in ui somehow.
ALSO: Question of the day! To obscurify the thumbnails or not. Edit panel does not do such...
*/


#include "llviewerprecompiledheaders.h"
#include "llfloaterinspect.h"
#include "llfloatertools.h"
#include "llselectmgr.h"
#include "lltoolcomp.h"
#include "lltoolmgr.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "lluictrlfactory.h"
#include "lltexturectrl.h"
#include "llviewerimage.h"
#include "llcontainerview.h"
#include "llscrollcontainer.h"
#include "llfloateravatarinfo.h"

#include "shcommandhandler.h"

class SHTextureInspect;
class SHTextureInspectHandler
{
	SHTextureInspect *pLast;
public:
	SHTextureInspectHandler() : pLast(NULL) {}
	void setSelected(SHTextureInspect *pSelected)
	{
		if(pLast==pSelected)
			return;
		SHTextureInspect *pPrev = pLast;
		pLast = pSelected;
		onSelected(pLast);
		if(pPrev)
			((LLTextureCtrl*)pPrev)->setFocus(false); //Virtual
	}
	SHTextureInspect *getSelected() const {return pLast;}

	virtual void onSelected(SHTextureInspect *pSelected) {};
};
class SHTextureInspect : public LLTextureCtrl, public LLInventoryObserver
{
	SHTextureInspectHandler *mHandler;
	bool mInInventory;
	LLColor4 mSelectedColor;
public:
	SHTextureInspect(LLUUID id, SHTextureInspectHandler *pHandler = NULL) : mHandler(pHandler),
		LLTextureCtrl(id.asString(), LLRect(0,64+BTN_HEIGHT_SMALL,64,0), id.asString().substr(0,8), id, id, id.asString())
	{
		mSelectedColor = LLColor4(1.f,1.f,1.f) - gColors.getColor("DefaultHighlightLight");
		mSelectedColor.setAlpha(.5f);
		gInventory.addObserver(this);
		updateInInventory();
	}
	~SHTextureInspect()
	{
		if(mHandler && mHandler->getSelected() == this)
			mHandler->setSelected(NULL);
		gInventory.removeObserver(this);
	}
	void	updateInInventory()
	{
		// Search inventory for this texture.
		LLViewerInventoryCategory::cat_array_t cats;
		LLViewerInventoryItem::item_array_t items;
		LLAssetIDMatches asset_id_matches(getImageAssetID());
		gInventory.collectDescendentsIf(LLUUID::null,cats,items,LLInventoryModel::INCLUDE_TRASH,asset_id_matches);
		mInInventory = items.count();
	}
	void	setFocus( BOOL b )
	{
		LLTextureCtrl::setFocus(b);
		if(mHandler)
		{
			if(b)
				mHandler->setSelected(this);
			else if(mHandler->getSelected()==this)
				mHandler->setSelected(NULL);
		}
	}
	LLPointer<LLViewerImage> getTextureImage(){return mTexturep;}
	virtual BOOL	handleMouseDown(S32 x, S32 y, MASK mask)
	{
		setFocus(TRUE);
		return TRUE;
	}
	virtual BOOL	handleDoubleClick(S32 x, S32 y, MASK mask)
	{
		//Doubleclick opens texture browser, but only if in inv. Otherwise too screenshot-exploitable.
		return mInInventory && LLTextureCtrl::handleMouseDown(x,y,mask);
	}
	virtual void	draw()
	{
		if(hasFocus())
		{
			S32 padding = BTN_HEIGHT_SMALL/2;
			LLRect border( -padding, getRect().getHeight()+padding, getRect().getWidth()+padding, 0 );
			gl_rect_2d( border, mSelectedColor, TRUE );
		}
		LLTextureCtrl::draw();
		if(mInInventory)
		{
			static LLUIImagePtr sImage = LLUI::getUIImage("inv_folder_plain_closed.tga");
			if(sImage.notNull())
			{
				const S32 width = getRect().getWidth();
				const S32 height = getRect().getHeight();
				const F32 new_size = llmin(width,height)*.3f;
				sImage->draw(width-new_size-1,BTN_HEIGHT_SMALL+1,new_size,new_size);
			}
		}
	}
	virtual void changed(U32 mask)
	{
		if((mask & LLInventoryObserver::ADD) || (mask & LLInventoryObserver::ALL) || (mask & LLInventoryObserver::REMOVE))
			updateInInventory();
	}
};

class SHFloaterInspectTextures : public LLFloater, public SHTextureInspectHandler, public LLViewerObjectListener
{
	static SHFloaterInspectTextures* sInstance;

	std::vector<LLUUID> mTextureList;
	std::vector<LLViewerObjectListenerScoped> mWatchList;

	LLContainerView *mTexContainer;
	LLScrollableContainerView *mScrollContainer;
	LLLineEditor *mUploader;
	LLLineEditor *mUploaderName;
	LLLineEditor *mUploadDate;
	LLButton *mShowProfile;

	S32 mObjCount, mRootCount;
	LLViewerObject *mFirstObj;

	std::string mSzUploader;
	std::string mSzUploadDate;

public:
	SHFloaterInspectTextures() : LLFloater(std::string("Inspect Textures"))
	{
		mTexContainer = NULL;
		mScrollContainer = NULL;
		mObjCount = 0;
		mRootCount = 0;
		mFirstObj = NULL;
		sInstance = this;
	
		LLUICtrlFactory::getInstance()->buildFloater(this, "sh_floater_inspect_textures.xml");		
	}
	~SHFloaterInspectTextures()
	{
		clearTextures();
		if(sInstance == this)
			sInstance = NULL;
	}
	BOOL isVisible() const 
	{
		return (!!sInstance);
	}
	BOOL postBuild()
	{
		mUploader = getChild<LLLineEditor>("uploader");
		mUploaderName = getChild<LLLineEditor>("uploader_name");
		mUploadDate = getChild<LLLineEditor>("upload_date");
		mShowProfile = getChild<LLButton>("showprofile");
		mShowProfile->setClickedCallback(callbackShowProfile, this);
		mShowProfile->setEnabled(false);

		LLRect scroll_rect(0, getRect().getHeight()-LLFLOATER_HEADER_SIZE, getRect().getWidth()-LLFLOATER_CLOSE_BOX_SIZE,0);
		if((mScrollContainer = 	getChild<LLScrollableContainerView>("texture_scroll",false,false))!=NULL)
			scroll_rect = mScrollContainer->getRect();
		else
		{
			scroll_rect.stretch(-1,-1);
			mScrollContainer = new LLScrollableContainerView(std::string("texture_scroll"), scroll_rect, (LLView*)NULL);
			mScrollContainer->setFollowsAll();
			addChild(mScrollContainer);
		}
		
		if((mTexContainer = mScrollContainer->getChild<LLContainerView>("texture_view",false,false))==NULL)
		{
			mTexContainer = new LLContainerView("texture_view",scroll_rect);
			mScrollContainer->addChild(mTexContainer);
		}
		//Bind together:
		mTexContainer->setScrollContainer(mScrollContainer);
		mScrollContainer->setScrolledView(mTexContainer);
		
		mTexContainer->showLabel(FALSE);
		mScrollContainer->setReserveScrollCorner(TRUE);

		llinfos << "Scroll_rect = ("<<scroll_rect.mLeft<<","<<scroll_rect.mTop<<","<<scroll_rect.mRight<<","<<scroll_rect.mBottom<<")"<<llendl;

		sortTextures();
		return TRUE;
	}
	static void hide()
	{
		if(sInstance)
			delete sInstance;
	}
	static void show(void* ignored)
	{
		if (!sInstance)	sInstance = new SHFloaterInspectTextures;
		sInstance->open();
		sInstance->refresh();
	}
	void reshape(S32 width, S32 height, BOOL called_from_parent = TRUE)
	{
		LLFloater::reshape(width,height,called_from_parent);
		sortTextures();
	}
	void sortTextures()
	{
		if(!mScrollContainer || !mTexContainer)return;

		LLRect scroll_rect = mScrollContainer->getRect();
		scroll_rect.mRight -= LLFLOATER_HEADER_SIZE;
		scroll_rect.mTop -= 2;

		if(!mTextureList.empty())
		{
			// 0,0 = TOP RIGHT
			const S32 column_padding = BTN_HEIGHT_SMALL;
			const S32 row_padding = BTN_HEIGHT_SMALL/2;
			const S32 x_offs = BTN_HEIGHT_SMALL/2;
			const S32 y_offs = BTN_HEIGHT_SMALL/2;
			const S32 entry_width = 64;
			const S32 entry_height = 64+BTN_HEIGHT_SMALL;
			const S32 full_width = scroll_rect.getWidth();

			S32 full_height = scroll_rect.getHeight();
			S32 col_pos = x_offs;
			S32 row_pos = y_offs;
			S32 col_count = (full_width+x_offs)/(entry_width+column_padding);
			if(!col_count)col_count = 1;

			//Adjust the rect
			{
				const S32 row_count = ceil(((F32)mTextureList.size()) / ((F32)col_count));
				const S32 req_height = row_padding + row_count * (entry_height + row_padding);
				if(full_height < req_height)
					full_height = req_height;
				scroll_rect.mBottom = 0;
				scroll_rect.mTop = full_height;
			}

			int col_index = 0;
			
			for(std::vector<LLUUID>::iterator ids=mTextureList.begin();ids!=mTextureList.end();++ids)
			{
				LLTextureCtrl *pTexture = mTexContainer->getChild<SHTextureInspect>((*ids).asString(),false,false);
				if(pTexture)
				{
					if(++col_index > col_count)
					{
						col_index = 1;
						col_pos = x_offs;
						row_pos += entry_height + row_padding;
					}
					LLRect pos_rect(0,0,entry_width,-entry_height);
					pos_rect.translate(scroll_rect.mLeft+col_pos,scroll_rect.mTop-row_pos);
					pTexture->setRect(pos_rect);
					col_pos += entry_width + column_padding;
				}
			}
		}
		mTexContainer->setRect(scroll_rect);
	}
	void addTexture(LLViewerImage *pImage)
	{
		if(!pImage || !mTexContainer) return;
		LLUUID id = pImage->getID();
		for(std::vector<LLUUID>::iterator ids=mTextureList.begin();ids!=mTextureList.end();++ids)
		{
			if((*ids)==id)
				return;
		}
		if(!mTexContainer->getChild<SHTextureInspect>(id.asString(),false,false))
		{
			mTextureList.push_back(id);
			SHTextureInspect *pCtrl = new SHTextureInspect(id,this);
			mTexContainer->addChild(pCtrl);
		}
	}
	void addObject(LLViewerObject *pObject)
	{
		if(!pObject)return;
		mWatchList.push_back(LLViewerObjectListenerScoped(this, pObject));
		for(int i = 0;i<pObject->getNumTEs();++i)
			addTexture(pObject->getTEImage(i));
	}
	void removeTexture(LLUUID &id)
	{
		LLView *pTexture = mTexContainer->getChild<SHTextureInspect>(id.asString(),false,false);
		if(pTexture)
			mTexContainer->removeChild(pTexture,true);
	}
	void clearTextures()
	{
		if(mTexContainer)
		{
			for(std::vector<LLUUID>::iterator ids=mTextureList.begin();ids!=mTextureList.end();++ids)
				removeTexture((*ids));
		}
		mTextureList.clear();
		mWatchList.clear();
	}
	void refresh()
	{
		bool is_selected = !!getSelected();
		LLUUID selected_id;
		if(is_selected)
			selected_id.set(getSelected()->getName());
		clearTextures();
		LLObjectSelection *mObjectSelection = LLSelectMgr::getInstance()->getSelection();
		if(mObjectSelection)
		{
			for (LLObjectSelection::iterator iter = mObjectSelection->begin();
				 iter != mObjectSelection->end(); iter++)
				addObject((*iter)->getObject());	

			mObjCount = mObjectSelection->getObjectCount();
			mRootCount = mObjectSelection->getRootObjectCount();
			mFirstObj = mObjectSelection->getFirstSelectedObject(NULL);
		}
		sortTextures();
		if(is_selected)
		{
			SHTextureInspect *pTexture = mTexContainer->getChild<SHTextureInspect>(selected_id.asString(),false,false);
			if(pTexture)pTexture->setFocus(true);
		}
	}
	void draw()
	{
		LLObjectSelection *mObjectSelection = LLSelectMgr::getInstance()->getSelection();
		if(mObjectSelection &&
			mObjCount != mObjectSelection->getObjectCount() || 
			mRootCount != mObjectSelection->getRootObjectCount() || 
			mFirstObj != mObjectSelection->getFirstSelectedObject(NULL))
			refresh();
		LLFloater::draw();
		if(getSelected())
		{
			LLViewerImage *pImage = getSelected()->getTextureImage();
			if(pImage)
			{
				std::map<std::string,std::pair<std::string,unsigned int> >::iterator it;
				if(mUploader->getText().empty() && (it=pImage->decodedComment.find("a"))!=pImage->decodedComment.end())
				{
					mShowProfile->setEnabled(true);
					mUploader->setText(it->second.first);
					gCacheName->get(LLUUID(mUploader->getText()), FALSE, callbackLoadAvatarName);
				}
				if(mUploadDate->getText().empty() && (it=pImage->decodedComment.find("z"))!=pImage->decodedComment.end())
				{
					std::string timestr;
					if(!timeStringToFormattedString(it->second.first,gSavedSettings.getString("TimestampFormat"),timestr))
						timestr = "Unknown";
					mUploadDate->setText(timestr);
				}
			}
		}
	}
	void setAvatarName(const LLUUID& id, const std::string& first, const std::string& last)
	{
		if(mUploader->getText() == id.asString() )
		{
			std::ostringstream fullname;
			fullname << first << " " << last;
			mUploaderName->setText(fullname.str());
		}
	}
	void ShowProfile()
	{
		LLUUID key = LLUUID(mUploader->getText());
		if (!key.isNull()) LLFloaterAvatarInfo::showFromDirectory(key);
	}
	void onSelected(SHTextureInspect *pSelected)
	{
		mUploader->setText(LLStringExplicit(""));
		mUploaderName->setText(LLStringExplicit(""));
		mUploadDate->setText(LLStringExplicit(""));
		mShowProfile->setEnabled(false);
	}
	/*virtual */void onObjectChanged(LLViewerObject *pObject, unsigned int changed)
	{
		if((changed & LLViewerObjectListener::TEX) || (changed & LLViewerObjectListener::REM))
			refresh();
		LLViewerObjectListener::onObjectChanged(pObject,changed);
	}
	static void callbackShowProfile(void *pData)
	{
		((SHFloaterInspectTextures*)pData)->ShowProfile();
	}
	static void callbackLoadAvatarName(const LLUUID& id, const std::string& first, const std::string& last, BOOL is_group, void* data)
	{
		if (sInstance)
			sInstance->setAvatarName(id,first,last);
	}
};
SHFloaterInspectTextures* SHFloaterInspectTextures::sInstance = NULL;

void handle_texture_inspector(void *)
{
	SHFloaterInspectTextures::show(NULL);
}
