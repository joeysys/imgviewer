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

#include "ImageReader.hpp"

#include <QFile>
#include <QFileInfo>
#include "ReaderGif.hpp"
#include "ReaderPng.hpp"
#include "ReaderJpeg.hpp"
#include "ReaderQt.hpp"

ImageReader::ImageReader(){
//	readers.push_back( std::make_unique<ReaderGif>() );
	readers.push_back( std::make_unique<ReaderPng>() );
	readers.push_back( std::make_unique<ReaderJpeg>() );
	readers.push_back( std::make_unique<ReaderQt>() );
	
	for( auto& reader : readers )
		for( auto ext : reader->extensions() )
			formats.insert( std::make_pair( ext.toLower(), reader.get() ) );
}

AReader::Error ImageReader::read( imageCache &cache, QString filepath ) const{
	QString ext = QFileInfo(filepath).suffix().toLower();
	
	AReader* reader = nullptr;
	auto it = formats.find( ext );
	if( it != formats.end() )
		reader = it->second;
	else
		return AReader::ERROR_TYPE_UNKNOWN;
	
	QFile file( filepath );
	cache.url = QUrl::fromLocalFile( filepath );
	QByteArray data;
	if( file.open( QIODevice::ReadOnly ) ){
		data = file.readAll();
		file.close();
	}
	else{
		cache.set_status( imageCache::EMPTY );
		return AReader::ERROR_NO_FILE;
	}
	
	auto u_data = reinterpret_cast<const uint8_t*>( data.constData() );
	AReader::Error err = reader->read( cache, u_data, data.size(), ext );
	
	if( err != AReader::ERROR_NONE ){
		//TODO: we should check for the error more specifically
		//Reading failed, lets try all the others and see if they can
		cache.reset();
		for( auto& r : readers ){
			if( !r->can_read( u_data, data.size(), "" ) )
				continue;
			
			if( r->read( cache, u_data, data.size(), "" ) == AReader::ERROR_NONE ){
				cache.error_msgs.append( QObject::tr( "Warning, wrong file extension" ) );
				return AReader::ERROR_NONE;
			}
			cache.reset();
		}
		
		cache.set_status( imageCache::INVALID );
	}
	
	return err;
}

QList<QString> ImageReader::supportedExtensions() const{
	QList<QString> extensions;
	for( auto format : formats )
		extensions << format.first;
	return extensions;
}
