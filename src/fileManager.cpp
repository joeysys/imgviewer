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

#include "fileManager.h"
#include "imageCache.h"


#include <QDir>
#include <QImageReader>
#include <QStringList>
#include <QCoreApplication>
#include <QTime>
#include <QSettings>

#include <qglobal.h>
#ifdef Q_OS_WIN
	#include <qt_windows.h>
	#include <Shlobj.h>
#endif


fileManager::fileManager(){
	connect( &loader, SIGNAL( image_fetched() ), this, SLOT( loading_handler() ) );
	connect( &watcher, SIGNAL( directoryChanged( QString ) ), this, SLOT( dir_modified( QString ) ) );
	
	current_file = -1;
	show_hidden = false;
	force_hidden = false;
	
#ifdef Q_OS_WIN
	//Set show_hidden on Windows to match Windows Explorer setting
	SHELLSTATE lpss;
	SHGetSetSettings( &lpss, SSF_SHOWALLOBJECTS | SSF_SHOWEXTENSIONS, false );
	
	if( lpss.fShowAllObjects )
		show_hidden = true;
	//TODO: also match "show extensions"? It is stored in: fShowExtensions
#endif
	
	//Initialize all supported image formats
	if( supported_file_ext.count() == 0 ){
		QList<QByteArray> supported = QImageReader::supportedImageFormats();
		for( int i=0; i<supported.count(); i++ )
			supported_file_ext << "*." + QString( supported.at( i ) );
	}
}

fileManager::~fileManager(){
	clear_cache();
}


void fileManager::set_files( QFileInfo file ){
	//Stop if it does not support file
	if( !supports_extension( file.fileName() ) ){
		clear_cache();
		return;
	}
	
	//If hidden, include hidden files
	force_hidden = file.isHidden();
	
	//Begin caching
	dir = file.dir().absolutePath();
	load_files( dir );
	init_cache( file.absoluteFilePath() );
	
	watcher.addPath( dir );
}

void fileManager::load_files( QDir dir ){
	//If hidden, include hidden files
	QDir::Filters filters = QDir::Files | QDir::Readable;
	if( show_hidden || force_hidden )
		filters |= QDir::Hidden;
	
	//Begin caching
	files = dir.entryInfoList( supported_file_ext , filters, QDir::Name | QDir::IgnoreCase | QDir::LocaleAware );
}

void fileManager::load_image( int pos ){
	if( cache[pos] )
		return;
	
	imageCache *img = new imageCache();
	if( loader.load_image( img, files[pos].filePath() ) ){
	qDebug( "loading image: %s", files[pos].filePath().toLocal8Bit().constData() );
		cache[pos] = img;
		if( pos == current_file )
			emit_file_changed();
	}
	else
		delete img;	//If it is already loading an image
}

void fileManager::init_cache( int start ){
	clear_cache();
	cache.clear();
	current_file = start;
	
	qDebug( "current_file: %d", current_file );
	if( current_file == -1 )
		return;
	
	cache.reserve( files.count() );
	for( int i=0; i<files.count(); i++ )
		cache << NULL;
	
	load_image( current_file );
	loading_handler();
}


bool fileManager::has_previous() const{
	return current_file > 0;
}

bool fileManager::has_next() const{
	return current_file + 1 < files.count();
}


void fileManager::next_file(){
	if( has_next() ){
		current_file++;
		emit_file_changed();
		loading_handler();
	}
}

void fileManager::previous_file(){
	if( has_previous() ){
		current_file--;
		emit_file_changed();
		loading_handler();
	}
}



void fileManager::loading_handler(){
	if( current_file == -1 )
		return;
	
	int loading_lenght = 10;
	for( int i=0; i<loading_lenght; i++ ){
		if( current_file + i < files.count() && !cache[current_file+i] ){
			load_image( current_file+i );
			break;
		}
		
		if( current_file - i >= 0 && !cache[current_file-i] ){
			load_image( current_file-i );
			break;
		}
	}
	
	//* Delete everything after loading length
	for( int i=current_file+loading_lenght; i<cache.count(); i++ ){
		loader.delete_image( cache[i] );
		cache[i] = NULL;
	}
	for( int i=current_file-loading_lenght; i>=0; i-- ){
		loader.delete_image( cache[i] );
		cache[i] = NULL;
	}
}


void fileManager::clear_cache(){
	watcher.removePaths( watcher.directories() );
	current_file = -1;
	emit_file_changed();
	for( int i=0; i<cache.count(); i++ )
		loader.delete_image( cache[i] );
	cache.clear();
}

//Make sure that this is done without caching
static bool file_exists( QFileInfo file ){
	file.setCaching( false );
	return file.exists();
}

struct oldCache{
	QFileInfo file;
	imageCache* cache;
};
void fileManager::dir_modified( QString dir ){
	if( !has_file() )
		return;
	
	//Wait shortly to ensure files have been updated
	//Solution by kshark27: http://stackoverflow.com/a/11487434/2248153
	QTime wait = QTime::currentTime().addMSecs( 200 );
	while( QTime::currentTime() < wait )
		QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
	
	//Find the file to show now, considering it might have disappeared
	QFileInfo new_file = files[current_file];
	if( !file_exists( new_file ) ){
		//Keep trying until we find a file which still exists
		int prev = current_file-1;
		int next = current_file+1;
		while( prev >= 0 || next < files.count() ){
			if( next < files.count() ){
				if( file_exists( files[next] ) ){
					new_file = files[next];
					break;
				}
				else
					next++;
			}
			
			if( prev >= 0 ){
				if( file_exists( files[prev] ) ){
					new_file = files[prev];
					break;
				}
				else
					prev--;
			}
		}
	}
	
	
	//Save imageCache's which might still be valid
	QList<oldCache> old;
	for( int i=0; i<cache.count(); i++ )
		if( cache[i] ){
			oldCache temp = { files[i], cache[i] };
			old << temp;
		}
	
	//Prepare the QLists
	cache.clear();
	load_files( new_file.dir() );
	cache.reserve( files.count() );
	for( int i=0; i<files.count(); i++ )
		cache << NULL;
	
	//Restore old elements
	for( int i=0; i<old.count(); i++ ){
		int new_index = files.indexOf( old[i].file );
		if( new_index != -1 ){
			cache[new_index] = old[i].cache;
			old[i].cache = NULL;
		}
	}
	
	//Set image position
	current_file = files.indexOf( new_file );
	emit_file_changed(); //The file and position could have changed
	
	if( current_file == -1 )
		return;
	
	//Now delete images which are no longer here
	//We can't do it earlier than emit file_changed(), as imageViewer needs to disconnect first
	for( int i=0; i<old.count(); i++ )
		if( old[i].cache )
			loader.delete_image( old[i].cache );
		
	//Start loading the new files
	if( !cache[ current_file ] )
		load_image( current_file );
	loading_handler();
}


bool fileManager::supports_extension( QString filename ) const{
	filename = filename.toLower();
	for( int i=0; i<supported_file_ext.count(); i++ )
		if( filename.endsWith( QString( supported_file_ext[i] ).remove( 0, 1 ) ) ) //Remove "*" from "*.ext"
			return true;
	return false;
}

void fileManager::delete_current_file(){
	if( !has_file() )
		return;
	
	//QFileWatcher will ensure that the list will be updated
	QFile::remove( files[current_file].absoluteFilePath() );
}


QString fileManager::file_name() const{
	if( !has_file() )
		return "No file!";
	
	//TODO: once we have a meta-data system, check if it contains a title
	return QString( "%1 - [%2/%3]" )
		.arg( files[current_file].fileName() )
		.arg( QString::number( current_file+1 ) )
		.arg( QString::number( files.count() ) )
		;
}

