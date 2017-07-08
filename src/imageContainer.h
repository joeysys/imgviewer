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

#ifndef IMAGECONTAINER_H
#define IMAGECONTAINER_H

#include <QWidget>
#include <QFileInfo>
#include <QList>
#include <QMenuBar>
#include <QSettings>
#include <QContextMenuEvent>

#include <memory>

#ifdef WIN_TOOLBAR
	#include <QtWinExtras>
#endif

class imageViewer;
class imageCache;
class windowManager;
class fileManager;
class Ui_controls;

class imageContainer: public QWidget{
	Q_OBJECT
	
	private:
		imageViewer *viewer;
		std::unique_ptr<windowManager> manager;
		Ui_controls *ui;
		
		std::unique_ptr<fileManager> files;
		QSettings settings;
		
		QMenuBar* menubar;
		QMenu* anim_menu;
		QMenu* context;
		bool menubar_autohide;
		
		class AnimButton{
			private:
				QIcon playing;
				QIcon paused;
				imageContainer* parent;
				
			public:
				AnimButton( imageContainer* parent );
				void setState( bool is_playing );
		} animation;
		
	private:
		bool is_fullscreen;
		bool is_cleanmode;
		bool was_maximized{ false };
		
		void create_menubar();
		void create_context();
		
	protected:
		virtual void keyPressEvent( QKeyEvent *event );
		virtual void dragEnterEvent( QDragEnterEvent *event );
		virtual void dropEvent( QDropEvent *event );
		virtual void focusInEvent( QFocusEvent* ){ hide_menubar(); }
		virtual void mousePressEvent( QMouseEvent* );
		virtual void contextMenuEvent( QContextMenuEvent* event );
		
	private slots:
		void updatePosition();
		void updateImageInfo();
		void update_toogle_btn();
		void hide_menubar();
		void resize_window( bool only_upscale=false );
		
		void update_file();
		void context_menu( QContextMenuEvent event ){
			contextMenuEvent( &event );
		}
		void copy_file();
		void copy_file_path();
	
	public slots:
		void toogle_animation();
		void toogle_cleanmode();
		void toogle_fullscreen();
		void center_window();
		
		void open_file();
		void next_file();
		void prev_file();
		void delete_file( bool ask=true );
		
		void open_help();
	
	public:
		explicit imageContainer( QWidget* parent = 0 );
		~imageContainer();
		
		void load_image( QFileInfo filepath );
		void load_images( QFileInfoList fileList );

#ifdef WIN_TOOLBAR
	private:
		QWinThumbnailToolButton* btn_prev{ nullptr };
		QWinThumbnailToolButton* btn_pause{ nullptr };
		QWinThumbnailToolButton* btn_next{ nullptr };
	public:
		void init_win_toolbar();
#endif
};


#endif
