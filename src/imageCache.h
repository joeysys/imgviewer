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

#ifndef IMAGECACHE_H
#define IMAGECACHE_H

#include <QObject>
#include <QImage>
#include <vector>
#include <lcms2.h>

class color;

class imageCache: public QObject{
	Q_OBJECT
	
	private:
		void init();
		static color* manager;
		
	private:
	//Variables containing info about the image(s)
		cmsHPROFILE profile;
		
		int frame_amount;
		std::vector<QImage> frames;
		int frames_loaded;
		
		bool animate;
		std::vector<int> frame_delays;
		int loop_amount;	//Amount of times the loop should continue looping
		
		long memory_size;
		
	//Info about loading
	public:
		enum status{
			EMPTY,	//Nothing loaded
			INVALID,	//Attempted loading, but failed
			INFO_READY,	//Info is valid
			FRAMES_READY,	//Some frames have been loaded
			LOADED	//All frames have been loaded
		};
	private:
		status current_status;
	public:
		status get_status() const{ return current_status; } //Current status
		int loaded() const{ return frames_loaded; }	//Amount of currently loaded frames
		
	public:
		explicit imageCache(){
			init();
		}
		~imageCache(){
			if( profile )
				cmsCloseProfile( profile );
		}
		
		void set_profile( cmsHPROFILE profile );
		void set_info( unsigned total_frames, bool is_animated=false, int loops=0 );
		void add_frame( QImage frame, unsigned delay );
		void set_fully_loaded();
		
		cmsHPROFILE get_profile() const{ return profile; }
		color* get_manager() const{ return manager; }
		long get_memory_size() const{ return memory_size; }	//Notice, this is a rough number, not accurate!
		
		//Animation info
		bool is_animated() const{ return animate; }
		int loop_count() const{ return loop_amount; }
		
		//Frame info
		int frame_count() const{ return frame_amount; }
		QImage frame( unsigned int idx ) const{ return idx < frames.size() ? frames[ idx ] : QImage(); }
		int frame_delay( unsigned int idx ) const{ return idx < frames.size() ? frame_delays[ idx ] : 0; } //How long a frame should be shown
	
	signals:
		void info_loaded();
		void frame_loaded( unsigned int idx );
};


#endif