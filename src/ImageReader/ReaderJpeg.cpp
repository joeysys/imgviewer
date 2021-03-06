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

#include "ReaderJpeg.hpp"
#include "../meta.h"

#include "jpeglib.h"
#include "jerror.h"

#include <QImage>
#include <QFile>
#include <memory>
#include <cstring>

struct MetaTest{
	unsigned marker_id;
	const uint8_t* prefix;
	unsigned check_length;
	unsigned length;

	bool validate( jpeg_marker_struct* marker ) const{
		if( marker->marker != marker_id )
			return false;
		if( marker->data_length < std::max( check_length, length ) )
			return false;
		return std::memcmp( marker->data, prefix, length ) == 0;
	}
};
static const uint8_t ICC_META_PREFIX[]
	= {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0u, 1u, 1u};
	//TODO: don't know if the last two should check
static const MetaTest ICC_META_TEST = { JPEG_APP0+2, ICC_META_PREFIX, 12, 14 };

static const uint8_t EXIF_META_PREFIX[]
	= {'E', 'x', 'i', 'f', 0u, 0u};
static const MetaTest EXIF_META_TEST = { JPEG_APP0+1, EXIF_META_PREFIX, 6, 0 };

//Error handlers
static void output_message( j_common_ptr cinfo ){
	char buf[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)( cinfo, buf );
	
	auto cache = static_cast<imageCache*>( cinfo->client_data );
	cache->error_msgs << QString::fromLatin1( buf );
}
static void error_exit( j_common_ptr cinfo ){
	(*cinfo->err->output_message)( cinfo );
	throw cinfo->err->msg_code;
}

static const uint8_t JPEG_MAGIC[] = { 0xff, 0xd8, 0xff };
bool ReaderJpeg::can_read( const uint8_t* data, unsigned length, QString ) const{
	if( length < 3 )
		return false;
	return std::memcmp( JPEG_MAGIC, data, 3 ) == 0;
}

class JpegDecompress{
	public: //TODO:
		jpeg_decompress_struct cinfo;
		jpeg_error_mgr jerr;
		
	public:
		JpegDecompress( const uint8_t* data, unsigned length ) {
			jpeg_create_decompress( &cinfo );
			jpeg_mem_src( &cinfo, const_cast<uint8_t*>(data), length );
			cinfo.err = jpeg_std_error( &jerr );
			cinfo.err->error_exit = error_exit;
			cinfo.err->output_message = output_message;
		}
		~JpegDecompress(){ jpeg_destroy_decompress( &cinfo ); }
		
		void saveMarker( const MetaTest& meta )
			{ jpeg_save_markers( &cinfo, meta.marker_id, 0xFFFF ); }
			//Marker lengths are only 16 bits, so 0xFFFF is max size
		
		void readHeader( bool what=true )
			{ jpeg_read_header( &cinfo, what ); }
		
		unsigned bytesPerLine() const
			{ return cinfo.output_width * cinfo.output_components; }
};


AReader::Error ReaderJpeg::read( imageCache &cache, const uint8_t* data, unsigned length, QString format ) const{
	if( !can_read( data, length, format ) )
		return ERROR_TYPE_UNKNOWN;
	
	try{
		cache.set_info( 1 );
		JpegDecompress jpeg( data, length );
		jpeg.cinfo.client_data = &cache;
		
		//Save application data, we are interested in ICC profiles and EXIF metadata
		jpeg.saveMarker( ICC_META_TEST );
		jpeg.saveMarker( EXIF_META_TEST );
		/* READ EVERYTHING!
		jpeg_save_markers( &jpeg.cinfo, JPEG_COM, 0xFFFF );
		for( unsigned i=0; i<16; i++ )
			jpeg_save_markers( &jpeg.cinfo, JPEG_APP0+i, 0xFFFF );
		//*/
		
		//Read header and set-up image
		jpeg.readHeader();
		jpeg_start_decompress( &jpeg.cinfo );
		
		bool is_gray;
		switch( jpeg.cinfo.out_color_components ){
			case 1: is_gray = true; break;
			case 3: is_gray = false; break;
			default: return ERROR_UNSUPPORTED;
		}
		QImage frame( jpeg.cinfo.output_width, jpeg.cinfo.output_height, QImage::Format_RGB32 );
		
		//Read image
		auto buffer = std::make_unique<JSAMPLE[]>( jpeg.bytesPerLine() );
		JSAMPLE* arr[1] = { buffer.get() };
		while( jpeg.cinfo.output_scanline < jpeg.cinfo.output_height ){
			auto out = (QRgb*)frame.scanLine( jpeg.cinfo.output_scanline );
			jpeg_read_scanlines( &jpeg.cinfo, arr, 1 );
			
			if( is_gray )
				for( unsigned ix=0; ix<jpeg.cinfo.output_width; ix++ )
					out[ix] = qRgb( buffer[ix], buffer[ix], buffer[ix] );
			else
				for( unsigned ix=0; ix<jpeg.cinfo.output_width; ix++ )
					out[ix] = qRgb( buffer[ix*3+0], buffer[ix*3+1], buffer[ix*3+2] );
		}
		
		//Check all markers
		for( auto marker = jpeg.cinfo.marker_list; marker; marker = marker->next ){
			//Check for and read ICC profile
			if( ICC_META_TEST.validate( marker ) ){
				cache.set_profile( ColorProfile::fromMem(
						marker->data        + ICC_META_TEST.length
					,	marker->data_length - ICC_META_TEST.length
					) );
			}
			if( EXIF_META_TEST.validate( marker ) ){
				meta exif(
						marker->data        + EXIF_META_TEST.length
					,	marker->data_length - EXIF_META_TEST.length
					);
				
				cache.set_orientation( exif.get_orientation() );
				
				//Read thumbnail
				cache.thumbnail = exif.get_thumbnail();
				//TODO: We actually want to read this BEFORE the full image ;)
				
				//TODO: Actually do something with this info. Perhaps check for a profile as well!
			}
			/* Save data to file for debugging
			QFile f( "Jpeg marker " + QString::number(marker->marker-JPEG_APP0) + ".bin" );
			f.open( QIODevice::WriteOnly );
			f.write( (char*)marker->data, marker->data_length );
			//*/
		}
		jpeg_finish_decompress( &jpeg.cinfo );
		
		cache.add_frame( frame, 0 );
		
		//Cleanup and return
		cache.set_fully_loaded();
		return ERROR_NONE;
	}
	catch( int err_code ){
		switch( err_code ){
			case JERR_NO_SOI: return ERROR_TYPE_UNKNOWN;
			default: return ERROR_FILE_BROKEN;
		};
	}
}

