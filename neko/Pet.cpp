// Pet.cpp: implementation of the CPet class.
//
//////////////////////////////////////////////////////////////////////

#include "Pet.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CPet::CPet()
{
	//prepare empty icon table
	m_nIcons = 0;
	m_nLastIcon = -1;
	m_hIcons = NULL;

	//set initial and old position
	m_ptPosition.x = 0;
	m_ptPosition.y = 0;
	m_ptOldPosition.x = -1;
	m_ptOldPosition.y = -1;

	//set initial size and scale
	m_sizeImage.cx = 0;
	m_sizeImage.cy = 0;
    m_fScale = 1.0f;

	//base (unscaled) icon size - sane 32x32 default until SetImages() knows better
	m_baseW = 32;
	m_baseH = 32;

	//set initial bounding box (none)
	SetRect( &m_rcBounds, -1, -1, -1, -1 );
}



CPet::~CPet()
{
	//delete the images
	DestroyImages();
}


//////////////////////////////////////////////////////////////////////
// Member Functions
//////////////////////////////////////////////////////////////////////


void CPet::SetImages( HICON* hIconTable, int nIcons )
{
	//delete current images
	DestroyImages();

	//we can't have 0 images
	if( nIcons <= 0 ) 
	{
		nIcons = 1;
#ifdef DEBUG
		OutputDebugString( "WARNING: CPet::SetImages() passed 0 as nIcons\n" );
#endif
	}

	//a NULL table is meaningless - stay in the empty state
	if( hIconTable == NULL )
	{
		return;
	}

    //FIXME: it might be possible to use CopyImage() passing it the *scaled* width and height
    //in combination with the LR_COPYFROMRESOURCE. This might fix the non-32x32 icon problem

	//allocate icon table and clear all entries
	m_hIcons = new HICON[nIcons];
	if( m_hIcons == NULL )
	{
		//out of memory: leave the object in its empty, safe state
		m_nIcons = 0;
		m_nLastIcon = -1;
		return;
	}
	for( int i = 0; i < nIcons; i++ ) m_hIcons[i] = NULL;

	//copy the icons, counting the ones that really succeeded
	int nCopied = 0;
	for( int i = 0; i < nIcons; i++ )
	{
		if( hIconTable[i] != NULL )
		{
			m_hIcons[i] = CopyIcon( hIconTable[i] );
			if( m_hIcons[i] != NULL ) nCopied++;
		}
	}

	//store icon table size and set last drawn icon index
	m_nIcons = nIcons;
	m_nLastIcon = -1;

	//if not a single icon could be copied (or the first one failed), we have
	//nothing to measure: discard the table and fall back to a 32x32 sprite
	if( nCopied == 0 || m_hIcons[0] == NULL )
	{
		DestroyImages();
		m_baseW = 32;
		m_baseH = 32;
		m_sizeImage.cx = int( m_baseW * m_fScale + 0.5f );
		m_sizeImage.cy = int( m_baseH * m_fScale + 0.5f );
		return;
	}

	//get size of the first icon (only from handles that are actually valid)
    ICONINFO ii = {0};
    BOOL bHaveSize = FALSE;
    if( GetIconInfo( m_hIcons[0], &ii ) )
    {
        BITMAP bm = {0};
        if( ii.hbmMask != NULL && GetObject( ii.hbmMask, sizeof(BITMAP), &bm ) != 0 )
        {
            if( bm.bmWidth > 0 && bm.bmHeight > 0 )
            {
                m_baseW = bm.bmWidth;
                m_baseH = bm.bmHeight;
                bHaveSize = TRUE;
            }
        }
        //only delete the bitmaps we know the API actually created
        if( ii.hbmMask != NULL ) DeleteObject( ii.hbmMask );
        if( ii.hbmColor != NULL ) DeleteObject( ii.hbmColor );
    }

    //if the icon info or bitmap was unavailable, use a sane default size
    if( !bHaveSize )
    {
        m_baseW = 32;
        m_baseH = 32;
    }

    //calculate scaled size from the unscaled base size
	m_sizeImage.cx = int( m_baseW * m_fScale + 0.5f );
	m_sizeImage.cy = int( m_baseH * m_fScale + 0.5f );
}



void CPet::DestroyImages()
{
	//delete all images, if any
	if( m_nIcons > 0 )
	{
		if( m_hIcons != NULL )
		{
			//delete all icons in the table
			for( int i = 0; i < m_nIcons; i++ ) DestroyIcon( m_hIcons[i] );

			//delete the table
			delete[] m_hIcons;
			m_hIcons = NULL;
		}
	}

	//always reset the bookkeeping so a second call cannot re-walk freed memory
	m_nIcons = 0;
	m_nLastIcon = -1;
}



void CPet::SetImage( int nImage )
{
	//only draw if the image has changed
	if( nImage != m_nLastIcon )
	{
		Erase();
		Draw( nImage );
		m_nLastIcon = nImage;
	}
}



void CPet::MoveTo(int nNewX, int nNewY)
{
	if( m_ptOldPosition.x == -1 )
	{
		//set old position to the current position as we've not been moved before
		m_ptOldPosition.x = nNewX;
		m_ptOldPosition.y = nNewY;
	}
	else
	{
		//store current position
		m_ptOldPosition.x = m_ptPosition.x;
		m_ptOldPosition.y = m_ptPosition.y;
	}

	//remove current
	Erase();

	//change current position
	m_ptPosition.x = nNewX;
	m_ptPosition.y = nNewY;

	//redraw
	Draw( m_nLastIcon );
}

void CPet::SetImageAndMoveTo(int nImage, int nNewX, int nNewY)
{
	//change image
	m_nLastIcon = nImage;

	//move
	MoveTo( nNewX, nNewY );
}


void CPet::SetScale(float fScale)
{
    if( fScale != 0.0f )
        m_fScale = fScale;
    else
        m_fScale = 1.0f;

    //recompute the cached sprite size so size and scale can never disagree.
    //If the icon table has not been set up yet there is nothing to recompute
    //(m_baseW/m_baseH still hold the 32x32 default, but m_sizeImage is 0).
    if( m_hIcons != NULL && m_nIcons > 0 )
    {
        m_sizeImage.cx = int( m_baseW * m_fScale + 0.5f );
        m_sizeImage.cy = int( m_baseH * m_fScale + 0.5f );
    }
}
