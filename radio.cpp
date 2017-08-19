#
/*
 *    Copyright (C) 2013, 2014, 2015, 2016, 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of the Qt-DAB (formerly SDR-J, JSDR).
 *    Qt-DAB is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    Qt-DAB is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Qt-DAB; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include	<QSettings>
#include	<QMessageBox>
#include	<QFileDialog>
#include	<QDebug>
#include	<QDateTime>
#include	<QFile>
#include	<QStringList>
#include	<QStringListModel>
#include	<QDir>
#include	"dab-constants.h"
#include	"radio.h"
#include	<fstream>
#include	<iostream>
#include	<numeric>
#include	<unistd.h>
#include	<vector>
#include	"msc-handler.h"
#include	"audiosink.h"
#include	"fft.h"
#include	"rawfiles.h"
#include	"wavfiles.h"
#ifdef	TCP_STREAMER
#include	"tcp-streamer.h"
#endif
#ifdef	HAVE_RTLSDR
#include	"rtlsdr-handler.h"
#endif
#ifdef	HAVE_SDRPLAY
#include	"sdrplay-handler.h"
#endif
#ifdef	HAVE_ELAD_S1
#include	"elad-handler.h"
#endif
#ifdef	__MINGW32__
#ifdef	HAVE_EXTIO
#include	"extio-handler.h"
#endif
#endif
#ifdef	HAVE_RTL_TCP
#include	"rtl_tcp_client.h"
#endif
#ifdef	HAVE_AIRSPY
#include	"airspy-handler.h"
#endif
#ifdef	TECHNICAL_DATA
#include	"ui_technical_data.h"
#endif
#ifdef	HAVE_SPECTRUM
#include	"spectrum-handler.h"
#endif

std::vector<size_t> get_cpu_times (void) {
	std::ifstream proc_stat ("/proc/stat");
	proc_stat. ignore (5, ' ');    // Skip the 'cpu' prefix.
	std::vector<size_t> times;
	for (size_t time; proc_stat >> time; times. push_back (time));
	return times;
}
 
bool get_cpu_times (size_t &idle_time, size_t &total_time) {
	const std::vector <size_t> cpu_times = get_cpu_times ();
	if (cpu_times. size () < 4)
           return false;
	idle_time  = cpu_times [3];
	total_time = std::accumulate (cpu_times. begin (), cpu_times. end (), 0);
	return true;
}
/**
  *	We use the creation function merely to set up the
  *	user interface and make the connections between the
  *	gui elements and the handling agents. All real action
  *	is initiated by gui buttons
  */
static	bool	thereisSound	= false;
static	int	frameErrors	= 0;

	RadioInterface::RadioInterface (QSettings	*Si,
	                                int16_t		tii_delay,
	                                int32_t		dataPort,
	                                bool		tracing,
	                                QWidget		*parent):
	                                        QMainWindow (parent),
	                                        my_ficHandler (this) {
int16_t	latency;
int16_t k;

	dabSettings		= Si;
#ifdef	TII_COORDINATES
	this	-> tii_delay	= tii_delay;
#endif
	this	-> tracing	= tracing;
// 	the setup for the generated part of the ui
	setupUi (this);

#ifdef	TECHNICAL_DATA
	dataDisplay	= new QFrame (NULL);
	techData. setupUi (dataDisplay);
	show_data		= false;
#ifdef	__MINGW32__
	techData. cpuLabel	-> hide ();
	techData. cpuMonitor	-> hide ();
#endif
	connect (showProgramData, SIGNAL (clicked (void)),
	         this, SLOT (toggle_show_data (void)));
#else
	showProgramData	-> hide ();	// do not show the button
#endif

	connect (saveEnsembleData, SIGNAL (clicked (void)),
	         this, SLOT (showEnsembleData (void)));
//
//	Before printing anything, we set
	setlocale (LC_ALL, "");
///	The default, most likely to be overruled
//
	inputDevice		= new virtualInput ();
	running			= false;
	scanning		= false;

/**	threshold is used in the phaseReference class 
  *	as threshold for checking the validity of the correlation result
  *	3 is a reasonable value
  */
	threshold	=
	           dabSettings -> value ("threshold", 3). toInt ();

	isSynced		= UNSYNCED;
//
//	latency is used to allow different settings for different
//	situations wrt the output buffering
	latency			=
	           dabSettings -> value ("latency", 1). toInt ();

	audioBuffer		= new RingBuffer<int16_t>(16 * 32768);
	dataBuffer		= new RingBuffer<uint8_t>(32768);
	ipAddress		= dabSettings -> value ("ipAddress", "127.0.0.1"). toString ();
	port			= dabSettings -> value ("port", 8888). toInt ();
//
	autoStart		= dabSettings -> value ("autoStart", 0). toInt () != 0;
	has_presetName		= dabSettings -> value ("has-presetName", 0). toInt () != 0;
	if (has_presetName) {
	   presetName		= dabSettings -> value ("presetname", ""). toString ();
	   if (presetName != QString (""))
	      autoStart = true;
	}
	currentName		= QString ("");

#ifdef	DATA_STREAMER
	dataStreamer		= new tcpServer (dataPort);
#endif
	streamoutSelector	-> hide ();
#ifdef	TCP_STREAMER
	soundOut		= new tcpStreamer	(audioBuffer,
	                                                 20040);
#else			// just sound out
	soundOut		= new audioSink		(latency,
	                                                 audioBuffer);
	((audioSink *)soundOut)	-> setupChannels (streamoutSelector);
	streamoutSelector	-> show ();
	bool err;
	QString h		=
	           dabSettings -> value ("soundchannel", "default"). toString ();
	k	= streamoutSelector -> findText (h);
	if (k != -1) {
	   streamoutSelector -> setCurrentIndex (k);
	   err = !((audioSink *)soundOut) -> selectDevice (k);
	}

	if ((k == -1) || err)
	   ((audioSink *)soundOut)	-> selectDefaultDevice ();
	connect (streamoutSelector, SIGNAL (activated (int)),
	         this,  SLOT (set_streamSelector (int)));
#endif

#ifdef  HAVE_SPECTRUM
        spectrumBuffer          = new RingBuffer<DSPCOMPLEX> (2 * 32768);
	iqBuffer		= new RingBuffer<DSPCOMPLEX> (2 * 1536);
        spectrumHandler = new spectrumhandler (this, dabSettings,
	                                       spectrumBuffer,
	                                       iqBuffer);
        spectrumHandler -> show ();
#endif

	QString defaultPath	= QDir::tempPath ();
	if (defaultPath. endsWith ("/"))
	   defaultPath. append ("qt-pictures/");
	else
	   defaultPath. append ("/qt-pictures/");

	picturesPath	=
	        dabSettings	-> value ("pictures", defaultPath). toString ();

	if ((picturesPath != "") && (!picturesPath. endsWith ("/")))
	   picturesPath. append ("/");
	QDir testdir (defaultPath);

	if (!testdir. exists ())
	   testdir. mkdir (defaultPath);

	QString t	=
	        dabSettings	-> value ("dabBand", "VHF Band III"). toString ();
	k	= bandSelector -> findText (t);
	if (k != -1) 
	   bandSelector -> setCurrentIndex (k);
	dabBand		= bandSelector -> currentText () == "VHF Band III" ?
	                                     BAND_III : L_BAND;

	theBand. setupChannels	(channelSelector, dabBand);

	t	= dabSettings	-> value ("dabMode", 1). toString ();
	k	= modeSelector -> findText (t);
	if (k != -1) 
	   modeSelector -> setCurrentIndex (k);

	saveSlides	= dabSettings -> value ("saveSlides", 1). toInt ();
	showSlides	= dabSettings -> value ("showPictures", 1). toInt ();
/**
  *	The actual work is done elsewhere: in ofdmProcessor
  *	and ofdmDecoder for the ofdm related part, ficHandler
  *	for the FIC's and mscHandler for the MSC.
  */
	my_mscHandler		= new mscHandler	(this,
	                                                 convert (modeSelector -> currentText ()),
	                                                 audioBuffer,
	                                                 dataBuffer,
	                                                 picturesPath);
/**
  *	The default for the ofdmProcessor depends on
  *	the input device, so changing the selection for an input device
  *	requires changing the ofdmProcessor.
  */
	my_ofdmProcessor = new ofdmProcessor   (this,
	                                        inputDevice,
	                                        convert (modeSelector -> currentText ()),
	                                        my_mscHandler,
	                                        &my_ficHandler,
	                                        threshold
#ifdef	HAVE_SPECTRUM
	                                       ,spectrumBuffer,
	                                        iqBuffer
#endif
	                                       );
	init_your_gui ();		// gui specific stuff

#ifdef	TECHNICAL_DATA
	QPalette p	= techData. ficError_display -> palette ();
	p. setColor (QPalette::Highlight, Qt::red);
	techData. ficError_display	-> setPalette (p);
	techData. frameError_display	-> setPalette (p);
	techData. rsError_display	-> setPalette (p);
	techData. aacError_display	-> setPalette (p);
	techData. rsError_display	-> hide ();
	techData. aacError_display	-> hide ();
	techData. motAvailable		-> 
                           setStyleSheet ("QLabel {background-color : red}");
#endif

	if (autoStart) {
	   if (has_presetName && (presetName != QString (""))) {
	      presetTimer. setSingleShot (true);
	      presetTimer. setInterval (8000);
	      connect (&presetTimer, SIGNAL (timeout (void)),
	               this, SLOT (setPresetStation (void)));
	      presetTimer. start (8000);
	   }

	   setStart ();
	}
}

	RadioInterface::~RadioInterface () {
	fprintf (stderr, "radioInterface is deleted\n");
}
//
/**
  *	\brief At the end, we might save some GUI values
  *	The QSettings could have been the class variable as well
  *	as the parameter
  */
void	RadioInterface::dumpControlState (QSettings *s) {
	if (s == NULL)	// cannot happen
	   return;

	if (has_presetName)
	   s	-> setValue ("presetname", currentName);
	
	s	-> setValue ("band", bandSelector -> currentText ());
	s	-> setValue ("channel",
	                      channelSelector -> currentText ());
	s	-> setValue ("device", deviceSelector -> currentText ());
	s	-> setValue ("soundchannel",
	                               streamoutSelector -> currentText ());
	s	-> setValue ("saveSlides", saveSlides ? 1 : 0);
	s	-> setValue ("showSlides", showSlides ? 1 : 0);
	s	-> sync ();
}

//
//
//	Most GUI specific things for the initialization are here
void	RadioInterface::init_your_gui (void) {
	ficBlocks		= 0;
	ficSuccess		= 0;
	syncedLabel		->
	               setStyleSheet ("QLabel {background-color : red}");
/**
  *	Devices can be included or excluded, setting is in the configuration
  *	files. Inclusion is reflected in the selector on the GUI.
  *	Note that HAVE_EXTIO is only useful for Windows
  */
#ifdef	HAVE_SDRPLAY
	deviceSelector	-> addItem ("sdrplay");
#endif
#ifdef	HAVE_RTLSDR
	deviceSelector	-> addItem ("dabstick");
#endif
#ifdef	HAVE_AIRSPY
	deviceSelector	-> addItem ("airspy");
#endif
#ifdef	HAVE_ELAD_S1
	deviceSelector	-> addItem ("elad-s1");
#endif
#ifdef  HAVE_EXTIO
    deviceSelector	-> addItem ("extio");
#endif
#ifdef	HAVE_RTL_TCP
	deviceSelector	-> addItem ("rtl_tcp");
#endif
	
/**	
  *	Happily, Qt is very capable of handling the representation
  *	of the ensemble and selecting an item
  */
	pictureLabel	= NULL;
	ensemble.setStringList (Services);
	ensembleDisplay	-> setModel (&ensemble);
	Services << " ";
	ensemble. setStringList (Services);
	ensembleDisplay	-> setModel (&ensemble);

/**
  *	The only timer we use is for displaying the running time.
  *	The number of seconds passed is kept in numberofSeconds
  */	
	numberofSeconds		= 0;
	displayTimer. setInterval (1000);
	connect (&displayTimer, SIGNAL (timeout (void)),
	         this, SLOT (updateTimeDisplay (void)));
	signalTimer. setSingleShot (true);
	signalTimer. setInterval (10000);
	connect (&signalTimer, SIGNAL (timeout (void)),
	         this, SLOT (No_Signal_Found (void)));
	         
//
	sourceDumping		= false;
	dumpfilePointer		= NULL;
	audioDumping		= false;
	audiofilePointer	= NULL;
//
/**
  *	we now handle the settings as saved by previous incarnations.
  */
	QString h	= dabSettings -> value ("channel", "12C"). toString ();
	int k		= channelSelector -> findText (h);
	if (k != -1) {
	   channelSelector -> setCurrentIndex (k);
	   selectChannel (h);
	}
	else
	   autoStart	= false;

	h		=
	           dabSettings -> value ("device", "no device"). toString ();
	k		= deviceSelector -> findText (h);
	if (k != -1) {
	   deviceSelector	-> setCurrentIndex (k);
	   setDevice 		(deviceSelector 	-> currentText ());
	}
	else
	   autoStart = false;

	
	connect (ensembleDisplay, SIGNAL (clicked (QModelIndex)),
	              this, SLOT (selectService (QModelIndex)));
	connect	(modeSelector, SIGNAL (activated (const QString &)),
	              this, SLOT (set_modeSelect (const QString &)));
	connect (startButton, SIGNAL (clicked (void)),
	              this, SLOT (setStart (void)));
	connect (quitButton, SIGNAL (clicked ()),
	              this, SLOT (TerminateProcess (void)));
	connect (deviceSelector, SIGNAL (activated (const QString &)),
	              this, SLOT (setDevice (const QString &)));
	connect (channelSelector, SIGNAL (activated (const QString &)),
	              this, SLOT (selectChannel (const QString &)));
	connect (bandSelector, SIGNAL (activated (const QString &)),
	              this, SLOT (set_bandSelect (const QString &)));
	connect (dumpButton, SIGNAL (clicked (void)),
	              this, SLOT (set_dumping (void)));
	connect (audioDumpButton, SIGNAL (clicked (void)),
	              this, SLOT (set_audioDump (void)));
	connect (resetButton, SIGNAL (clicked (void)),
	              this, SLOT (autoCorrector_on (void)));
	connect	(scanButton, SIGNAL (clicked (void)),
	              this, SLOT (set_Scanning (void)));
//	display the version
	QString v = "Qt-DAB " ;
	v. append (CURRENT_VERSION);
	versionName	-> setText (v);
//	and start the timer
	displayTimer. start (1000);
}

///////////////////////////////////////////////////////////////////////////////
//	
//	The public slots are called from other places within the dab software
//	so please provide some implementation, perhaps an empty one
//
//	a slot called by the ofdmprocessor
void	RadioInterface::set_fineCorrectorDisplay (int v) {
	finecorrectorDisplay	-> display (v);
}

//	a slot called by the ofdmprocessor
void	RadioInterface::set_coarseCorrectorDisplay (int v) {
	coarsecorrectorDisplay	-> display (v);
}

//	If the ofdm processor has waited for a period of N frames
//	to get a start of a synchronization,
//	it sends a signal to the GUI handler
//	If "scanning" is "on" we hop to the next frequency on
//	the list

void	RadioInterface::signalTimer_out (void) {
	No_Signal_Found ();
}

void	RadioInterface::No_Signal_Found (void) {
	signalTimer. stop ();
	if (!scanning)
	   return;
//
//	we stop the thread from running,
//	Increment the frequency
//	and restart
	my_ofdmProcessor -> stop ();
	while (!my_ofdmProcessor -> isFinished ())
	   usleep (10000);
	Increment_Channel ();
	clearEnsemble ();
	my_ofdmProcessor	-> start ();
	signalTimer. start (10000);
}
//
//	In case the scanning button was pressed, we
//	set it off as soon as we have a signal found
void	RadioInterface::Yes_Signal_Found (void) {
	signalTimer. stop ();
	if (!scanning)
	   return;
	set_Scanning ();
}

void	RadioInterface::set_Scanning	(void) {
	presetTimer. stop ();
	setStereo (false);
	if (!running)
	   return;

	scanning	= !scanning;
	if (my_ofdmProcessor != NULL)
	   my_ofdmProcessor -> set_scanMode (scanning);
	if (scanning) {
	   scanButton -> setText ("scanning");
	   Increment_Channel ();
	   signalTimer. start (10000);
	}
	else
	scanButton -> setText ("Scan band");
}
//
//	Increment channel is called during scanning.
//	The approach taken here is to increment the current index
//	in the combobox and select the new frequency.
//	To avoid disturbance, we disconnect the combobox
//	temporarily, since otherwise changing the channel would
//	generate a signal
void	RadioInterface::Increment_Channel (void) {
int32_t	tunedFrequency;
int	cc	= channelSelector -> currentIndex ();

	cc	+= 1;
	if (cc >= channelSelector -> count ())
	   cc = 0;
//
//	To avoid reaction of the system on setting a different value
	disconnect (channelSelector, SIGNAL (activated (const QString &)),
	              this, SLOT (selectChannel (const QString &)));
	channelSelector -> setCurrentIndex (cc);
	tunedFrequency	=
	         theBand. Frequency (dabBand, channelSelector -> currentText ());
	inputDevice	-> setVFOFrequency (tunedFrequency);

	connect    (channelSelector, SIGNAL (activated (const QString &)),
	              this, SLOT (selectChannel (const QString &)));
}

///////////////////////////////////////////////////////////////////////////
/**
  *	clearEnsemble
  *	on changing settings, we clear all things in the gui
  *	related to the ensemble.
  *	The function is called from "deep" within the handling code
  *	Potentially a dangerous approach, since the fic handler
  *	might run in a separate thread and generate data to be displayed
  */
void	RadioInterface::clearEnsemble	(void) {
//
//	it obviously means: stop processing
	my_mscHandler		-> stopProcessing ();
	my_ficHandler. clearEnsemble ();
	my_ofdmProcessor	-> coarseCorrectorOn ();
	my_ofdmProcessor	-> reset ();

	clear_showElements	();
}

//
//	a slot, called by the fic/fib handlers
void	RadioInterface::addtoEnsemble (const QString &s) {
	Services << s;
	Services. removeDuplicates ();
	ensemble. setStringList (Services);
	ensembleDisplay	-> setModel (&ensemble);
}

//
///	a slot, called by the fib processor
void	RadioInterface::nameofEnsemble (int id, const QString &v) {
QString s;
	(void)v;
	ensembleId		-> display (id);
	ensembleLabel		= v;
	ensembleName		-> setText (v);
	my_ofdmProcessor	-> coarseCorrectorOff ();
	Yes_Signal_Found ();
}

///////////////////////////////////////////////////////////////////////
/**
  *	\brief show_successRate
  *	a slot, called by the MSC handler to show the
  *	percentage of frames that could be handled
  */
void	RadioInterface::show_frameErrors (int s) {
	if (tracing && thereisSound)
	   frameErrors	+= s;
#ifdef	TECHNICAL_DATA
	techData. frameError_display	-> setValue (100 - 4 * s);
#endif
}

void	RadioInterface::show_rsErrors (int s) {
#ifdef	TECHNICAL_DATA
	techData. rsError_display	-> setValue (100 - 4 * s);
#endif
}
	
void	RadioInterface::show_aacErrors (int s) {
#ifdef	TECHNICAL_DATA
	techData. aacError_display	-> setValue (100 - 4 * s);
#endif
}
	
void	RadioInterface::show_ficSuccess (bool b) {
#ifdef	TECHNICAL_DATA
	if (b)
	   ficSuccess ++;
	if (++ficBlocks >= 100) {
	   techData. ficError_display	-> setValue (ficSuccess);
	   ficSuccess	= 0;
	   ficBlocks	= 0;
	}
#endif
}

void	RadioInterface::show_motHandling (bool b) {
#ifdef	TECHNICAL_DATA
	if (b) {
	   techData. motAvailable -> 
	               setStyleSheet ("QLabel {background-color : green}");
	}
	else {
	   techData. motAvailable ->
	               setStyleSheet ("QLabel {background-color : red}");
	}
#endif
}
	
///	called from the ofdmDecoder, which computed this for each frame
void	RadioInterface::show_snr (int s) {
	snrDisplay	-> display (s);
}

///	just switch a color, obviously GUI dependent, but called
//	from the ofdmprocessor
void	RadioInterface::setSynced	(char b) {
	if (isSynced == b)
	   return;

	isSynced = b;
	switch (isSynced) {
	   case SYNCED:
	      syncedLabel -> 
	               setStyleSheet ("QLabel {background-color : green}");
	      break;

	   default:
	      syncedLabel ->
	               setStyleSheet ("QLabel {background-color : red}");
	      break;
	}
}

//	showLabel is triggered by the message handler
//	the GUI may decide to ignore this
void	RadioInterface::showLabel	(QString s) {
	if (running)
	   dynamicLabel	-> setText (s);
}

void	RadioInterface::setStereo	(bool s) {
	if (s) 
	   stereoLabel -> 
	               setStyleSheet ("QLabel {background-color : green}");

	else
	   stereoLabel ->
	               setStyleSheet ("QLabel {background-color : red}");
}
//
void	checkDir (QString &s) {
int16_t	ind	= s. lastIndexOf (QChar ('/'));
int16_t	i;
QString	dir;
	if (ind == -1)		// no slash, no directory
	   return;

	for (i = 0; i < ind; i ++)
	   dir. append (s [i]);

	if (QDir (dir). exists ())
	   return;
	QDir (). mkpath (dir);
}
//	showMOT is triggered by the MOT handler,
//	the GUI may decide to ignore the data sent
//	since data is only sent whenever a data channel is selected
void	RadioInterface::showMOT		(QByteArray data,
	                                 int subtype, QString pictureName) {
	const char *type;
	if (!running)
	   return;
	if (pictureLabel == NULL) 
	   pictureLabel	= new QLabel (NULL);

	type = subtype == 0 ? "GIF" :
	       subtype == 1 ? "JPG" :
//	       subtype == 1 ? "JPEG" :
	       subtype == 2 ? "BMP" : "PNG";

	QPixmap p;
	p. loadFromData (data, type);
	if (saveSlides && (pictureName != QString (""))) {
	   QString pictureAddress = picturesPath;
	   pictureAddress. append (pictureName);
	   pictureAddress	= QDir::toNativeSeparators (pictureAddress);
	   checkDir (pictureAddress);
	   FILE *x = fopen (pictureAddress. toLatin1 (). data (), "w+b");
	   if (x == NULL)
	      fprintf (stderr, "cannot write file %s\n",
	                            pictureAddress. toLatin1 (). data ());
	   else {
	      fprintf (stderr, "going to write file %s\n",
	                            pictureAddress. toLatin1 (). data ());
	      (void)fwrite (data. data (), 1, data.length (), x);
	      fclose (x);
	   }
	}

//	pictureLabel -> setFrameRect (QRect (0, 0, p. height (), p. width ()));

	if (showSlides) {
	   pictureLabel ->  setPixmap (p);
	   pictureLabel ->  show ();
	}
}
//
//	sendDatagram is triggered by the ip handler,
void	RadioInterface::sendDatagram	(int length) {
uint8_t localBuffer [length];
	if (dataBuffer -> GetRingBufferReadAvailable () < length) {
	   fprintf (stderr, "Something went wrong\n");
	   return;
	}
	dataBuffer -> getDataFromBuffer (localBuffer, length);
	if (running)
	   dataOut_socket. writeDatagram ((const char *)localBuffer, length,
	                                   QHostAddress (ipAddress),
	                                   port);
}

void	RadioInterface::handle_tdcdata (int length) {
uint8_t localBuffer [length];
int16_t i;
	if (dataBuffer -> GetRingBufferReadAvailable () < length) {
	   fprintf (stderr, "Something went wrong\n");
	   return;
	}
	dataBuffer -> getDataFromBuffer (localBuffer, length);
	for (i = 0; i < 8; i ++)
	   fprintf (stderr, "%2x", localBuffer [i] & 0xFF);
	fprintf (stderr, "\n");
#ifdef	DATA_STREAMER
	if (running)
	   dataStreamer -> sendData (localBuffer, length);
#endif
}

/**
  *	\brief changeinConfiguration
  *	No idea yet what to do, so just give up
  *	with what we were doing. The user will -eventually -
  *	see the new configuration from which he can select
  */
void	RadioInterface::changeinConfiguration	(void) {
	if (running) {
	   soundOut		-> stop ();
	   inputDevice		-> stopReader ();
	   inputDevice		-> resetBuffer ();
	   running		= false;
	}
	clear_showElements	();
}

void	RadioInterface::newAudio	(int rate) {
	if (running) {
	   soundOut	-> audioOut (rate);
	}
}

//
//	This function is only used in the Gui to clear
//	the details of a selection
void	RadioInterface::clear_showElements (void) {
	Services = QStringList ();
	ensemble. setStringList (Services);
	ensembleDisplay		-> setModel (&ensemble);
	my_ficHandler. clearEnsemble ();

	ensembleLabel		= QString ();
	ensembleName		-> setText (ensembleLabel);
	dynamicLabel		-> setText ("");
	
//	Then the various displayed items
	ensembleName		-> setText ("   ");
#ifdef TECHNICAL_DATA
	techData. frameError_display	-> setValue (0);
	techData. rsError_display	-> setValue (0);
	techData. aacError_display	-> setValue (0);
	techData. ficError_display	-> setValue (0);
	techData. ensemble 		-> setText (QString (""));
	techData. programName		-> setText (QString (""));
	techData. frequency		-> display (0);
	techData. bitrateDisplay	-> display (0);
	techData. startAddressDisplay	-> display (0);
	techData. lengthDisplay		-> display (0);
	techData. subChIdDisplay	-> display (0);
//	techData. protectionlevelDisplay -> display (0);
	techData. uepField		-> setText (QString (""));
	techData. ASCTy			-> setText (QString (""));
	techData. language		-> setText (QString (""));
	techData. programType		-> setText (QString (""));
	techData. motAvailable		-> 
	               setStyleSheet ("QLabel {background-color : red}");
	techData. transmitter_coordinates -> setText (" ");
#endif
	snrDisplay		-> display (0);
	if (pictureLabel != NULL)
	   delete pictureLabel;
	pictureLabel = NULL;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//	
//	Private slots relate to the modeling of the GUI
//
/**
  *	\brief setStart is a function that is called after pushing
  *	the start button.
  *	if "autoStart" == true, then the initializer will start
  *
  */
void	RadioInterface::setStart	(void) {
bool	r = 0;
	if (running)		// only react when not running yet
	   return;

	r = inputDevice		-> restartReader ();
	qDebug ("Starting %d\n", r);
	if (!r) {
	   QMessageBox::warning (this, tr ("Warning"),
	                               tr ("Opening  input stream failed\n"));
	   return;
	}
//
//	Of course, starting the machine will generate a new instance
//	of the ensemble, so the listing - if any - should be cleared
	clearEnsemble ();		// the display
	running = true;
}

/**
  *	\brief TerminateProcess
  *	Pretty critical, since there are many threads involved
  *	A clean termination is what is needed, regardless of the GUI
  */
void	RadioInterface::TerminateProcess (void) {
	running		= false;
#ifdef	DATA_STREAMER
	fprintf (stderr, "going to close the dataStreamer\n");
	delete		dataStreamer;
#endif
	displayTimer. stop ();
	signalTimer.  stop ();
	presetTimer.  stop ();
	if (sourceDumping) {
	   my_ofdmProcessor	-> stopDumping ();
	   sf_close (dumpfilePointer);
	}

	if (audioDumping) {
	   soundOut	-> stopDumping ();
	   sf_close (audiofilePointer);
	}

	inputDevice		-> stopReader ();	// might be concurrent
	my_mscHandler		-> stopHandler ();	// might be concurrent
	my_ofdmProcessor	-> stop ();		// definitely concurrent
	soundOut		-> stop ();
#ifdef	TECHNICAL_DATA
	dataDisplay	->  hide ();
#endif
//	everything should be halted by now
	dumpControlState (dabSettings);
	delete		soundOut;
	delete		inputDevice;
#ifdef	TECHNICAL_DATA
//	delete		dataDisplay;
#endif
//	everything should be halted by now
	delete	my_mscHandler;	
	delete	my_ofdmProcessor;
#ifdef	HAVE_SPECTRUM
	delete	spectrumHandler;
	delete	spectrumBuffer;
	delete	iqBuffer;
#endif
	if (pictureLabel != NULL)
	   delete pictureLabel;
	pictureLabel = NULL;		// signals may be pending, so careful
	close ();
	fprintf (stderr, ".. end the radio silences\n");
}

//
/**
  *	\brief selectChannel
  *	Depending on the GUI the user might select a channel
  *	or some magic will cause a channel to be selected
  */

void	RadioInterface::selectChannel (QString s) {
int32_t	tunedFrequency;
bool	localRunning	= running;

	presetTimer. stop ();
	setStereo (false);
	if (scanning)
	   set_Scanning ();	// switch it off

	if (localRunning) {
	   soundOut	-> stop ();
	   inputDevice		-> stopReader ();
	   inputDevice		-> resetBuffer ();
	}

	clear_showElements ();

	tunedFrequency	= theBand. Frequency (dabBand, s);
	inputDevice	-> setVFOFrequency (tunedFrequency);

	if (localRunning) {
	   inputDevice	 -> restartReader ();
	   my_ofdmProcessor	-> reset ();
	   my_mscHandler	-> stopProcessing ();
	   running	 = true;
	}
	dabSettings	-> setValue ("channel", s);
}

static size_t previous_idle_time	= 0;
static size_t previous_total_time	= 0;

void	RadioInterface::updateTimeDisplay (void) {
	numberofSeconds ++;
	int16_t	numberHours	= numberofSeconds / 3600;
	int16_t	numberMinutes	= (numberofSeconds / 60) % 60;
#ifdef	TII_COORDINATES
	if (numberofSeconds % tii_delay == 0)
	   if (my_ofdmProcessor != NULL)
	      my_ofdmProcessor	-> set_tiiCoordinates ();
#endif
	QString text = QString ("runtime ");
	text. append (QString::number (numberHours));
	text. append (" hr, ");
	text. append (QString::number (numberMinutes));
	text. append (" min");
	timeDisplay	-> setText (text);
#ifndef	__MINGW32__
#ifdef	TECHNICAL_DATA
	if ((numberofSeconds % 2) == 0) {
	   size_t idle_time, total_time;
	   get_cpu_times (idle_time, total_time);
	   const float idle_time_delta = idle_time - previous_idle_time;
           const float total_time_delta = total_time - previous_total_time;
           const float utilization = 100.0 * (1.0 - idle_time_delta / total_time_delta);
	   techData. cpuMonitor -> display (utilization);
           previous_idle_time = idle_time;
           previous_total_time = total_time;
	   if (tracing && thereisSound) {
	      fprintf (stderr, " frameErrors = %d, missed samples for audio %d\n",
	                          frameErrors,((audioSink *)soundOut) -> missed ());
	      frameErrors	= 0;
	   }
	}
#endif
#endif
}

void	RadioInterface::autoCorrector_on (void) {
//	first the real stuff
	clear_showElements	();
	my_ficHandler. clearEnsemble ();
	my_ofdmProcessor	-> coarseCorrectorOn ();
	my_ofdmProcessor	-> reset ();
	my_mscHandler		-> stopProcessing ();
	
}

//
//	One can imagine that the mode of operation is just selected
//	by the "ini" file, it is pretty unlikely that one changes
//	the mode during operation.
//	In a next version it will go out
void	RadioInterface::set_modeSelect (const QString &Mode) {

	if (sourceDumping) {
	   my_ofdmProcessor -> stopDumping ();
	   sf_close (dumpfilePointer);
	   sourceDumping = false;
	   dumpButton	-> setText ("Dump to raw file");
	}

	if (audioDumping) {
	   soundOut	-> stopDumping ();
	   sf_close (audiofilePointer);
	   audioDumping	= false;
	   audioDumpButton -> setText ("Save audio");
	}

	running	= false;
	soundOut		-> stop ();
	inputDevice		-> stopReader ();
	my_ofdmProcessor	-> stop ();
//
//	we have to create a new ofdmprocessor with the correct
//	settings of the parameters.
	delete	my_mscHandler;
	my_ficHandler. setBitsforMode	(convert (Mode));
	my_mscHandler		= new mscHandler    (this,
	                                             convert (Mode),
	                                             audioBuffer,
	                                             dataBuffer,
	                                             picturesPath);
	delete my_ofdmProcessor;
	my_ofdmProcessor	= new ofdmProcessor  (this,
	                                              inputDevice,
	                                              convert (Mode),
	                                              my_mscHandler,
	                                              &my_ficHandler,
	                                              threshold
#ifdef	HAVE_SPECTRUM
	                                              ,spectrumBuffer,
	                                              iqBuffer
#endif
	                                             );
//	and wait for someone push the setStart
}
//
//	One can imagine that the band of operation is just selected
//	by the "ini" file, it is pretty unlikely that one changes
//	the band during operation, however, .....
void	RadioInterface::set_bandSelect (QString s) {
	if (running) {
	   running	= false;
	   inputDevice	-> stopReader ();
	   inputDevice	-> resetBuffer ();
	   soundOut	-> stop ();
	   usleep (100);
	   clearEnsemble ();
	}

	if (s == "BAND III")
	   dabBand	= BAND_III;
	else
	   dabBand	= L_BAND;
	theBand. setupChannels (channelSelector, dabBand);
}
/**
  *	\brief setDevice
  *	setDevice is called trough a signal from the gui
  *	Operation is in three steps: 
  *	   first dumping of any kind is stopped
  *	   second the previously loaded device is stopped
  *	   third, the new device is initiated, but not started
  */
//
//	setDevice is called from the GUI. Other GUI's might have a preselected
//	single device to go with, then if suffices to extract some
//	code specific to that device
void	RadioInterface::setDevice (QString s) {
QString	file;
//
///	first stop dumping
	presetTimer. stop ();
	dynamicLabel    -> setText (" ");
        if (pictureLabel != NULL)
           delete pictureLabel;
        pictureLabel    = NULL;

	if (sourceDumping) {
	   my_ofdmProcessor -> stopDumping ();
	   sf_close (dumpfilePointer);
	   sourceDumping = false;
	   dumpButton	-> setText ("Dump to raw file");
	}

	if (audioDumping) {
	   soundOut	-> stopDumping ();
	   sf_close (audiofilePointer);
	   audioDumping	= false;
	   audioDumpButton -> setText ("Save audio");
	}
///	indicate that we are not running anymore
	running		= false;
	soundOut	-> stop ();
//
//
///	select. For all it holds that:
	my_ofdmProcessor -> stop ();
	delete	my_ofdmProcessor;
	inputDevice	-> stopReader ();
	delete	inputDevice;
	dynamicLabel	-> setText ("");

///	OK, everything quiet, now let us see what to do
#ifdef	HAVE_AIRSPY
	if (s == "airspy") {
	   try {
	      inputDevice	= new airspyHandler (dabSettings);
	      showButtons ();
	      selectChannel	(channelSelector -> currentText ());
	   }
	   catch (int e) {
	      QMessageBox::warning (this, tr ("Warning"),
                                   tr ("Airspy or Airspy mini not found\n"));
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else
#endif
#ifdef HAVE_EXTIO
//	extio is - in its current settings - for Windows, it is a
//	wrap around the dll
	if (s == "extio") {
	   try {
	      inputDevice = new extioHandler (dabSettings);
	      showButtons ();
	      selectChannel (channelSelector -> currentText() );
	   }
	   catch (int e) {
	      QMessageBox::warning (this, tr ("Warning"),
	                            tr ("extio: no luck\n") );
	      inputDevice = new virtualInput();
	      resetSelector ();
	   }
	}
	else
#endif
#ifdef HAVE_RTL_TCP
//	RTL_TCP might be working. 
	if (s == "rtl_tcp") {
	   try {
	      inputDevice = new rtl_tcp_client (dabSettings);
	      showButtons ();
	      selectChannel (channelSelector -> currentText() );
	   }
	   catch (int e) {
	      QMessageBox::warning (this, tr ("Warning"),
	                           tr ("rtl_tcp: no luck\n") );
	      inputDevice = new virtualInput();
	      resetSelector ();
	   }
	}
	else
#endif
#ifdef	HAVE_SDRPLAY
	if (s == "sdrplay") {
	   try {
	      inputDevice	= new sdrplayHandler (dabSettings);
	      showButtons ();
	      selectChannel	(channelSelector -> currentText ());
	   }
	   catch (int e) {
	      QMessageBox::warning (this, tr ("Warning"),
	                               tr ("SDRplay: no library or device\n"));
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else
#endif
#ifdef	HAVE_ELAD_S1
	if (s == "elad-s1") {
	   try {
	      inputDevice	= new eladHandler (dabSettings);
	      showButtons ();
	      selectChannel	(channelSelector -> currentText ());
	   }
	   catch (int e) {
	      QMessageBox::warning (this, tr ("Warning"),
	                               tr ("elad-s1: no library or device\n"));
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else
#endif
#ifdef	HAVE_RTLSDR
	if (s == "dabstick") {
	   try {
	      inputDevice	= new rtlsdrHandler (dabSettings);
	      showButtons ();
	      selectChannel	(channelSelector -> currentText ());
	   }
	   catch (int e) {
	      QMessageBox::warning (this, tr ("Warning"),
                                   tr ("DAB stick not found! Please use one with RTL2832U or similar chipset!\n"));
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else
#endif
//
//	We always have fileinput!!
	if (s == "file input (.iq)") {
	   file		= QFileDialog::getOpenFileName (this,
	                                                tr ("Open file ..."),
	                                                QDir::homePath (),
	                                                tr ("iq data (*.iq)"));
	   file		= QDir::toNativeSeparators (file);
	   try {
	      inputDevice	= new rawFiles (file);
	      hideButtons ();
	   }
	   catch (int e) {
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else
	if (s == "file input (.raw)") {
	   file		= QFileDialog::getOpenFileName (this,
	                                                tr ("Open file ..."),
	                                                QDir::homePath (),
	                                                tr ("raw data (*.raw)"));
	   file		= QDir::toNativeSeparators (file);
	   try {
	      inputDevice	= new rawFiles (file);
	      hideButtons ();
	   }
	   catch (int e) {
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else
	if (s == "file input (.sdr)") {
	   file		= QFileDialog::getOpenFileName (this,
	                                                tr ("Open file ..."),
	                                                QDir::homePath (),
	                                                tr ("raw data (*.sdr)"));
	   file		= QDir::toNativeSeparators (file);
	   try {
	      inputDevice	= new wavFiles (file);
	      hideButtons ();
	   }
	   catch (int e) {
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	}
	else {	// s == "no device" 
//	and as default option, we have a "no device"
	   inputDevice	= new virtualInput ();
	}

#ifdef	HAVE_SPECTRUM
	spectrumHandler	-> setBitDepth (inputDevice -> bitDepth ());
#endif
//	we have a new device, so we can re-create the ofdmProcessor
//	Note: the fichandler and mscHandler remain unchanged
	my_ofdmProcessor	= new ofdmProcessor   (this,
	                                               inputDevice,
	                                               convert (modeSelector -> currentText ()),
	                                               my_mscHandler,
	                                               &my_ficHandler,
	                                               threshold
#ifdef	HAVE_SPECTRUM
	                                              ,spectrumBuffer,
	                                               iqBuffer
#endif
	                                              );
}

//
//	Selecting a service is easy, the fib is asked to
//	hand over the relevant data in two steps
void	RadioInterface::selectService (QModelIndex s) {
QString	currentProgram = ensemble. data (s, Qt::DisplayRole). toString ();
	presetTimer. stop ();
	selectService (currentProgram);
}
//
//	Might be called from the GUI as well as with an internal call
void	RadioInterface::selectService (QString s) {
	if ((my_ficHandler. kindofService (s) != AUDIO_SERVICE) &&
	    (my_ficHandler. kindofService (s) != PACKET_SERVICE))
	return;
	currentName = s;
	setStereo (false);
	soundOut	-> stop ();
	thereisSound	= false;
#ifdef	TECHNICAL_DATA
	dataDisplay	-> hide ();
	techData. rsError_display	-> hide ();
	techData. aacError_display	-> hide ();
	techData. motAvailable		-> 
	               setStyleSheet ("QLabel {background-color : red}");
	techData. transmitter_coordinates -> setText (" ");
#endif
	switch (my_ficHandler. kindofService (s)) {
	   case AUDIO_SERVICE:
	      { audiodata d;
	        my_ficHandler. dataforAudioService (s, &d);
	        if (d. bitRate == 0) {
                   QMessageBox::warning (this, tr ("Warning"),
 	                               tr ("unknown bitrate for this program\n"));
 	           return;
 	        }
#ifdef	TECHNICAL_DATA
	        techData. ensemble	-> setText (ensembleLabel);
	        techData. programName	-> setText (s);
	        techData. frequency	-> display ((uint32_t)(inputDevice -> getVFOFrequency ()) / 1000000.0);
	        techData. bitrateDisplay -> display (d. bitRate);
	        techData. startAddressDisplay -> display (d. startAddr);
	        techData. lengthDisplay	-> display (d. length);
	        techData. subChIdDisplay -> display (d. subchId);
	        uint16_t h = d. protLevel;
	        QString protL;
	        if (!d. shortForm) {
	           protL = "EEP ";

               protL. append (QString::number ((h & 03) + 1));
               if ((h & (1 << 2)) == 0)
                  protL. append ("-A");
	           else
                  protL. append ("-B");


	        }
	        else  {
	           h = h & 03;
	           protL = "UEP ";
	           protL. append (QString::number (h));
	        }
	        techData. uepField -> setText (protL);
//	        techData. protectionlevelDisplay -> display (h);   // no more needed
	        techData. ASCTy -> setText (d. ASCTy == 077 ? "DAB+" : "DAB");
	        if (d. ASCTy == 077) {
	           techData. rsError_display -> show ();
	           techData. aacError_display -> show ();
	        }
	        techData. language ->
	           setText (the_textMapper. get_programm_language_string (d. language));
	        techData. programType ->
	            setText (the_textMapper. get_programm_type_string (d. programType));
	         if (show_data)
	            dataDisplay -> show ();
#endif
	        my_mscHandler	-> set_audioChannel (&d);
	        soundOut	-> restart ();
	        thereisSound	= true;
	        showLabel (QString (" "));
	        break;
	      }

	   case PACKET_SERVICE:
	      {  packetdata d;
	         my_ficHandler. dataforDataService (s, &d);
	         if ((d.  DSCTy == 0) || (d. bitRate == 0)) {
	            fprintf (stderr, "d. DSCTy = %d, d. bitRate = %d\n",
	                               d. DSCTy, d. bitRate);
	            QMessageBox::warning (this, tr ("sdr"),
 	                               tr ("still insufficient data for this service\n"));

	            return;
	         }
	         my_mscHandler	-> set_dataChannel (&d);
	         switch (d. DSCTy) {
	            default:
	               showLabel (QString ("unimplemented Data"));
	               break;
	            case 5:
	               fprintf (stderr, "selected apptype %d\n", 
	                                                 d. appType);
	               showLabel (QString ("Transp. Channel not implemented"));
	               break;
	            case 60:
	               showLabel (QString ("MOT partially implemented"));
	               break;
	            case 59: {
	                  QString text = QString ("Embedded IP: UDP data to ");
	                  text. append (ipAddress);
	                  text. append (" ");
	                  QString n = QString::number (port);
	                  text. append (n);
	                  showLabel (text);
	               }
	               break;
	            case 44:
	               showLabel (QString ("Journaline"));
	               break;
	         }
	        break;
	      }

	   default:
               QMessageBox::warning (this, tr ("Warning"),
 	                               tr ("unknown service\n"));
	      return;
	}

	if (pictureLabel != NULL)
	   delete pictureLabel;
	pictureLabel = NULL;
}
//

/**	In case selection of a device did not work out for whatever
  *	reason, the device selector is reset to "no device"
  *	Qt will trigger on the change of value in the deviceSelector
  *	which will cause selectdevice to be called again (while we
  *	are in the middle, so we first disconnect the selector
  *	from the slot. Obviously, after setting the index of
  *	the device selector, we connect again
  */
void	RadioInterface::resetSelector (void) {
	disconnect (deviceSelector, SIGNAL (activated (const QString &)),
	            this, SLOT (setDevice (const QString &)));
int	k	= deviceSelector -> findText (QString ("Select device"));
	if (k != -1) { 		// should always happen
	   deviceSelector -> setCurrentIndex (k);
	}
	fprintf (stderr, "deviceSelector is reset %d\n", k);
	connect (deviceSelector, SIGNAL (activated (const QString &)),
	         this, SLOT (setDevice (const QString &)));
}

#ifdef	HAVE_SPECTRUM
//	signal, received from ofdm_decoder that a buffer is filled
//	with amount values ready for display
void	RadioInterface::showIQ	(int amount) {
	if (spectrumHandler != NULL)
	   spectrumHandler	-> showIQ (amount);
}

void	RadioInterface::showSpectrum	(int32_t amount) {
	if (spectrumHandler != NULL)
	   spectrumHandler	-> showSpectrum (amount,
	                                         inputDevice -> getVFOFrequency ());
}

#ifdef	__QUALITY
void	RadioInterface::showQuality	(float q) {
	if (spectrumHandler != NULL)
	   spectrumHandler	-> showQuality (q);
}
#endif
#endif


///	switch for dumping on/off
void	RadioInterface::set_dumping (void) {
SF_INFO *sf_info	= (SF_INFO *)alloca (sizeof (SF_INFO));

	if (sourceDumping) {
	   my_ofdmProcessor	-> stopDumping ();
	   sf_close (dumpfilePointer);
	   sourceDumping = false;
	   dumpButton	-> setText ("Dump to raw file");
	   return;
	}

//	Dumping is GUI dependent and may be ignored
	QString file = QFileDialog::getSaveFileName (this,
	                                     tr ("Save file ..."),
	                                     QDir::homePath (),
	                                     tr ("raw data (*.sdr)"));
	if (file == QString (""))	// apparently cancelled
	   return;
	file	= QDir::toNativeSeparators (file);
	if (!file.endsWith (".sdr", Qt::CaseInsensitive))
	   file.append (".sdr");
	sf_info	-> samplerate	= INPUT_RATE;
	sf_info	-> channels	= 2;
	sf_info	-> format	= SF_FORMAT_WAV | SF_FORMAT_PCM_16;

	dumpfilePointer	= sf_open (file. toLatin1 (). data (),
	                                   SFM_WRITE, sf_info);
	if (dumpfilePointer == NULL) {
	   qDebug () << "cannot open " << file. toLatin1 (). data ();
	   return;
	}

	dumpButton	-> setText ("writing");
	sourceDumping		= true;
	my_ofdmProcessor -> startDumping (dumpfilePointer);
}
///	audiodumping is similar
void	RadioInterface::set_audioDump (void) {
SF_INFO	*sf_info	= (SF_INFO *)alloca (sizeof (SF_INFO));

	if (audioDumping) {
	   soundOut	-> stopDumping ();
	   sf_close (audiofilePointer);
	   audioDumping = false;
	   audioDumpButton	-> setText ("Save audio");
	   return;
	}

	QString file = QFileDialog::getSaveFileName (this,
	                                        tr ("Save file ..."),
	                                        QDir::homePath (),
	                                        tr ("PCM wave file (*.wav)"));
	if (file == QString (""))
	   return;
	file		= QDir::toNativeSeparators (file);
	if (!file.endsWith (".wav", Qt::CaseInsensitive))
	   file.append (".wav");
	sf_info		-> samplerate	= 48000;
	sf_info		-> channels	= 2;
	sf_info		-> format	= SF_FORMAT_WAV | SF_FORMAT_PCM_16;

	audiofilePointer	= sf_open (file. toLatin1 (). data (),
	                                   SFM_WRITE, sf_info);
	if (audiofilePointer == NULL) {
	   qDebug () << "Cannot open " << file. toLatin1 (). data ();
	   return;
	}

	audioDumpButton		-> setText ("WRITING");
	audioDumping		= true;
	soundOut		-> startDumping (audiofilePointer);
}

void	RadioInterface:: set_streamSelector (int k) {
#ifndef	TCP_STREAMER
	((audioSink *)(soundOut)) -> selectDevice (k);
#else
	(void)k;
#endif
}

#ifdef	TECHNICAL_DATA
void	RadioInterface::toggle_show_data (void) {
	show_data	= !show_data;
	if (show_data)
	   dataDisplay -> show ();
	else
	   dataDisplay -> hide ();
}
#endif

uint8_t	RadioInterface::convert (QString s) {
	if (s == "Mode 1")
	   return 1;
	if (s == "Mode 2")
	   return 2;
	if (s == "Mode 3")
	   return 3;
	if (s == "Mode 4")
	   return 4;
	return 1;
}

void	RadioInterface::showButtons	(void) {
	scanButton	-> show ();
	bandSelector	-> show ();
	modeSelector	-> show ();
	channelSelector	-> show	();
#ifdef	TECHNICAL_DATA
	techData. frequency	-> show ();
#endif

}

void	RadioInterface::hideButtons	(void) {
	scanButton	-> hide ();
	bandSelector	-> hide ();
	modeSelector	-> hide ();
	channelSelector	-> hide ();
#ifdef	TECHNICAL_DATA
	techData. frequency	-> hide ();
#endif
}

void	RadioInterface::setSyncLost	(void) {
//	fprintf (stderr, "synchronization lost\n");
//	my_ofdmProcessor	-> reset ();
}

void	RadioInterface::showCoordinates (float lat, float lon) {
QString a, b;
	a	= QString::number (lat);
	b	= QString::number (lon);
	a. append ("  ");
	a. append (b);
#ifdef  TECHNICAL_DATA
	techData. transmitter_coordinates -> setText (a);
#endif
}

#include	"country-codes.h"

const char *code_to_string (uint8_t ecc, uint8_t countryId) {
int16_t	i = 0;

	while (countryTable [i]. ecc != 0) {
	   if ((countryTable [i]. ecc == ecc) &&
	       (countryTable [i]. countryId == countryId))
	      return countryTable [i]. countryName;
	   i ++;
	}
	return "          ";
}

const char *uep_rates  [] = {NULL, "7/20", "2/5", "1/2", "3/5"};
const char *eep_Arates [] = {NULL, "1/4",  "3/8", "1/2", "3/4"};
const char *eep_Brates [] = {NULL, "4/9",  "4/7", "4/6", "4/5"};

void	RadioInterface::showEnsembleData (void) {
uint8_t	countryId;
int16_t	i;
uint8_t ecc_byte	= my_ficHandler. get_ecc ();
QString	ensembleLabel	= my_ficHandler. get_ensembleName ();
int32_t	ensembleId	= my_ficHandler. get_ensembleId ();
QString currentChannel	= channelSelector -> currentText ();
int32_t	frequency	= inputDevice -> getVFOFrequency ();
bool	firstData;

	if (ensembleLabel == QString (""))
	   return;
	QString fileName = QFileDialog::getSaveFileName (this,
	                                        tr ("Save file ..."),
	                                        QDir::homePath (),
	                                        tr ("Text (*.txt)"));
	fileName	= QDir::toNativeSeparators (fileName);
	FILE *file_P	= fopen (fileName. toLatin1 (). data (), "w");

	if (file_P == NULL) {
	   fprintf (stderr, "Could not open file %s\n", fileName. toLatin1 (). data ());
	   return;
	}

	fprintf (file_P, "%s; ensembleId %X; channel %s; frequency %d; \n\n",
	                  ensembleLabel. toLatin1 (). data (),
	                  ensembleId,
	                  currentChannel. toLatin1 (). data (),
	                  frequency / 1000);
	                
	fprintf (file_P, "\nAudio services\nprogram name;country;serviceId;sub channel;start address;length (CU); bit rate;DAB/DAB+; prot level; code rate; language; program type\n\n");
	for (i = 0; i < 64; i ++) {
	   audiodata d;
	   my_ficHandler. dataforAudioService (i, &d);
	   if (!d. defined)
	      continue;
	   uint16_t h = d. protLevel;
	   QString protL;
	   QString codeRate;
	   if (!d. shortForm) {
	      protL = "EEP ";

          protL. append (QString::number ((h & 03) + 1));
	      if ((h & (1 << 2)) == 0) {
             protL. append ("-A");
	         codeRate = eep_Arates [(h & 03) + 1];
	      }
	      else {
             protL. append ("-B");
	         codeRate = eep_Brates [(h & 03) + 1];
	      }
          h = (h & 03) + 1;
	   }
	   else  {
	      h = h & 03;
	      protL = "UEP ";
	      protL. append (QString::number (h));
	      codeRate = uep_rates [h + 1];
	   }

	   countryId = (d. serviceId >> 12) & 0xF;
	   fprintf (file_P, "%s;%s;%X;%d;%d;%d;%d;%s;%s;%s;%s;%s;\n",
	                     d. serviceName. toLatin1(). data (),
	                     code_to_string (ecc_byte, countryId),
	                     d. serviceId,
	                     d. subchId,
	                     d. startAddr,
	                     d. length,
	                     d. bitRate,
	                     d. ASCTy == 077 ? "DAB+" : "DAB",
	                     protL. toLatin1 (). data (),
	                     codeRate. toLatin1 (). data (),
	                     the_textMapper. get_programm_language_string (d. language),
	                     the_textMapper. get_programm_type_string (d. programType) );
	}

	firstData	= true;
	for (i = 0; i < 64; i ++) {
	   packetdata d;
	   my_ficHandler. dataforDataService (i, &d);
	   if (!d. defined)
	      continue;

	   if (firstData) {
	      fprintf (file_P, "\n\n\nData Services\nprogram name;;serviceId;sub channel;start address;length (CU); bit rate; FEC; prot level; appType ; ; \n\n");
	      firstData = false;
	   }
	   
	   uint16_t h = d. protLevel;
	   QString protL;
	   QString codeRate;
	   if (!d. shortForm) {
	      protL = "EEP ";
          protL. append (QString::number ((h & 03) + 1));
	      if ((h & (1 << 2)) == 0) {
             protL. append ("-A");
	         codeRate = eep_Arates [(h & 03) + 1];
	      }
	      else {
             protL. append ("-B");
	         codeRate = eep_Brates [(h & 03) + 1];
	      }
          h = (h & 03) + 1;
	   }
	   else  {
	      h = h & 03;
	      protL = "UEP ";
	      protL. append (QString::number (h));
	      codeRate = uep_rates [h + 1];
	   }
	   countryId = (d. serviceId >> (5 * 4)) & 0xF;
	   fprintf (file_P, "%s;%s;%X;%d;%d;%d;%d;%d;%s;%d;;;\n",
	                     d. serviceName. toLatin1(). data (),
	                     code_to_string (ecc_byte, countryId),
	                     d. serviceId,
	                     d. subchId,
	                     d. startAddr,
	                     d. length,
	                     d. bitRate,
	                     d. FEC_scheme,
	                     protL. toLatin1 (). data (),
	                     d. appType);
	}
	fclose (file_P);
}

void	RadioInterface::setPresetStation (void) {
int16_t i;

	if (ensembleLabel == QString (""))
	   return;

	if (presetName == QString (""))		// should not happen
	   return;

	for (i = 0; i < Services. size (); i ++)
	   if (Services. at (i). contains (presetName)) {
	      selectService (presetName);
	      return;
	   }
}

