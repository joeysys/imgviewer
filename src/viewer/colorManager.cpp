/*
	This file is part of imgviewer.

	imgviewer is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	imgviewer is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with imgviewer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "colorManager.h"

#include <lcms2.h>
#include <QApplication>
#include <QtConcurrent>
#include <QImage>
using namespace std;

#include <qglobal.h>
#ifdef Q_OS_WIN
	#include <qt_windows.h>
#else
	#include <QDesktopWidget>
	#include <QX11Info>

	#include <xcb/xcb.h>
	#include <xcb/xproto.h>
	#include <xcb/xcb_atom.h>

	#include <string>
	#include <vector>

	
	vector<colorManager::MonitorIcc> get_x11_icc(){
		xcb_connection_t *conn = QX11Info::connection();
		xcb_window_t window = QX11Info::appRootWindow();
		int monitor_amount = QApplication::desktop()->screenCount();
		
		//xcb stuff for each monitor connected to the system
		struct x_mon{
			xcb_atom_t atom;
			xcb_intern_atom_cookie_t atom_cookie;
			xcb_get_property_cookie_t prop_cookie;
			
			x_mon( xcb_intern_atom_cookie_t atom_cookie ) : atom_cookie(atom_cookie) { }
		};
		vector<x_mon> x_mons;
		x_mons.reserve( monitor_amount );
		
		//Get cookies for atoms
		for( int i=0; i<monitor_amount; i++ ){
			string name = "_ICC_PROFILE";
			if( i > 0 )
				name += ( "_" + QString::number( i ) ).toUtf8().constData();
			
			x_mons.emplace_back( xcb_intern_atom( conn, 1, name.size(), name.c_str() ) );
		}
		
		//Get atoms
		for( auto& x_mon : x_mons ){
			auto reply = xcb_intern_atom_reply( conn, x_mon.atom_cookie, nullptr );
			if( reply ){
				x_mon.atom = reply->atom;
				free( reply );
			}
			else
				x_mon.atom = XCB_NONE;
		}
		
		//Get property cookies
		for( auto& x_mon : x_mons )
			x_mon.prop_cookie = xcb_get_property( conn, 0, window, x_mon.atom, XCB_ATOM_CARDINAL, 0, UINT_MAX );
		
		//Load profiles
		vector<colorManager::MonitorIcc> iccs;
		iccs.reserve( monitor_amount );
		for( auto& x_mon : x_mons ){
			auto reply = xcb_get_property_reply( conn, x_mon.prop_cookie, nullptr );
			
			colorManager::MonitorIcc icc( nullptr );
			if( reply ){
				icc.profile = cmsOpenProfileFromMem( xcb_get_property_value( reply ), reply->length );
				free( reply );
			}
			iccs.push_back( icc );
		}
		
		return iccs;
	}

#endif


colorManager::colorManager(){
	//Init default profiles
	p_srgb = cmsCreate_sRGBProfile();
	
#ifdef Q_OS_WIN
	//Try to grab it from the Windows APIs
	DISPLAY_DEVICE disp;
	DWORD index = 0;
	disp.cb = sizeof(DISPLAY_DEVICE);
	while( EnumDisplayDevices( nullptr, index++, &disp, 0 ) != 0 ){
		//Temporaries for converting
		DWORD size = 250;
		wchar_t icc_path[size];
		char path_ancii[size*2];
		
		//Get profile
		HDC hdc = CreateDC( nullptr, disp.DeviceName, icc_path, nullptr ); //TODO: what is icc_path used here for?
		GetICMProfile( hdc, &size, icc_path );
		DeleteDC( hdc );
		
		//Read and add
		wcstombs( path_ancii, icc_path, size*2 );
		monitors.emplace_back( cmsOpenProfileFromFile( path_ancii, "r") );
	}
#else
	#ifdef Q_OS_UNIX
		monitors = get_x11_icc();
	#else
		QString app_path = QApplication::applicationDirPath();
		monitors.push_back( MonitorIcc( cmsOpenProfileFromFile( (app_path + "/1.icc").toLocal8Bit().constData(), "r") ) );
		qDebug( "Warning, no proper support for color management on this platform" );
	//TODO:
	#endif
#endif
	
	//Create default transforms
	for( auto& monitor : monitors )
		//If there is a profile, make a default transform.
		if( monitor.profile )
			monitor.transform_srgb = cmsCreateTransform(
					p_srgb
				,	TYPE_BGRA_8	//This might be incorrect on some systems???
				,	monitor.profile
				,	TYPE_BGRA_8
				,	INTENT_PERCEPTUAL
				,	0
				);
}
colorManager::~colorManager(){
	//Delete profiles and transforms
	cmsCloseProfile( p_srgb );
	for( auto& monitor : monitors ){
		if( monitor.profile )
			cmsCloseProfile( monitor.profile );
		if( monitor.transform_srgb )
			cmsDeleteTransform( monitor.transform_srgb );
	}
}


//Load a profile from memory and get its transform
cmsHTRANSFORM colorManager::get_transform( cmsHPROFILE in, unsigned monitor ) const{
	if( in && monitors.size() > monitor ){
		//Get monitor profile, or fall-back to sRGB
		cmsHPROFILE use = monitors[monitor].profile;
		if( !use )
			use = p_srgb;
			
		//Create transform
		return cmsCreateTransform(
				in
			,	TYPE_BGRA_8
			,	use
			,	TYPE_BGRA_8
			,	INTENT_PERCEPTUAL
			,	0
			);
	}
	else
		return nullptr;
}

void colorManager::do_transform( QImage& img, unsigned monitor, cmsHTRANSFORM transform ) const{
	if( !transform && monitors.size() > monitor )
		transform = monitors[monitor].transform_srgb;
	
	if( transform ){
		//Make sure the image is in the correct pixel format
		QImage::Format format = img.format();
		//qDebug( "Format: %d", (int)format );
		
		//For indexed images, we only need to transform the color table
		if( format == QImage::Format_Indexed8 ){
			auto colors = img.colorTable();
			cmsDoTransform( transform, colors.data(), colors.data(), colors.size() );
			img.setColorTable( colors );
			return;
		}
		
		//Make sure the image is in a format we support
		if( format != QImage::Format_RGB32 && format != QImage::Format_ARGB32 )
			img = img.convertToFormat( QImage::Format_ARGB32 );
		
		//Convert
		vector<char*> lines;
		for( int i=0; i < img.height(); i++ )
			lines.push_back( (char*)img.scanLine( i ) );
		QtConcurrent::blockingMap( lines.begin(), lines.end()
			,	[&]( char* line ){ cmsDoTransform( transform, line, line, img.width() ); }
			);
	}
}

