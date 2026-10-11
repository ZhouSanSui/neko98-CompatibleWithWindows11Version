// Pet.h: interface for the CPet class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PET_H__A69EBAA0_385D_11D2_9FF9_00001C192944__INCLUDED_)
#define AFX_PET_H__A69EBAA0_385D_11D2_9FF9_00001C192944__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class CPet {

public:
	//constructor/destructor
	CPet();
	virtual ~CPet();

	//public configuration functions
	virtual void SetImages( HICON* hIconTable, int nIcons );
	virtual void SetScale( float fScale );

	//public interface
	virtual void SetImageAndMoveTo( int nImage, int nNewX, int nNewY );
	virtual void MoveTo( int nNewX, int nNewY );
	virtual void SetImage( int nImage );

    virtual void DrawOnTarget( int x, int y, HICON hIcon ) {}; //no default implementation

    //show/hide the pet. No-op here; CTransparentPet overrides it to actually
    //show or hide the layered window. TRUE = show, FALSE = hide.
    virtual void SetVisible( BOOL bShow ) {};

    //Called when the desktop geometry changed (resolution change, monitor
    //hot-plug, colour-depth change) WITHOUT the configuration changing. The
    //default does nothing. CTransparentPet overrides it to re-read the virtual
    //desktop size and rebuild/drop its surface, so a geometry event no longer
    //has to destroy and reconstruct every cat (which re-extracted 40 icons and
    //teleported the pet).
    virtual void OnDisplayChange() {};

    //Called at the end of a burst of drawing to let the renderer commit once.
    //The default does nothing. CTransparentPet overrides it to flush a deferred
    //repaint, so "move the cat" + "drop a footprint" in the same tick cost a
    //single composite instead of two. BeginDraw()/FlushDraw() are a matched
    //pair and must always be balanced.
    virtual void BeginDraw() {};
    virtual void FlushDraw() {};

	//data member access
	inline RECT   GetBoundsRect()  { return m_rcBounds; };
	inline POINT& GetPosition()	   { return m_ptPosition; };
	inline POINT& GetOldPosition() { return m_ptOldPosition; };
    inline SIZE   GetSize()        { return m_sizeImage; };

    //The size to use when deciding whether the cat has ARRIVED at a target.
    //This must be DPI-INDEPENDENT. GetSize() returns the size the sprite is
    //drawn at right now, and on a mixed-DPI desktop that changes the instant the
    //cat crosses a monitor boundary. Deriving the arrival point from the current
    //drawn size moves the "arrived" line every time the DPI changes, so a target
    //just past such a boundary can never be reached: the cat crosses, the
    //remaining distance flips sign, and it runs back and forth along the screen
    //edge forever. CTransparentPet overrides this with its DPI-independent base
    //size; for a plain pet the drawn size is already DPI-independent.
    virtual SIZE GetAimSize() { return m_sizeImage; }

protected:

	//pure virtual functions
	virtual void Erase() = 0;
	virtual void Draw( int nImage ) = 0;

	//image destruction
	virtual void DestroyImages();

	//icon information
	HICON* m_hIcons;
	int m_nIcons;
	int m_nLastIcon;

	//position & size information
	POINT m_ptPosition;
	POINT m_ptOldPosition;
	SIZE  m_sizeImage;
	float m_fScale;

	//unscaled size of the base icon, used to recompute m_sizeImage whenever
	//the scale changes so the cached size can never disagree with m_fScale
	int m_baseW;
	int m_baseH;

	//bounding box information
	RECT m_rcBounds;
};

#endif // !defined(AFX_PET_H__A69EBAA0_385D_11D2_9FF9_00001C192944__INCLUDED_)
