#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <ftrScanAPI.h>

typedef struct tagBITMAPINFOHEADER{
        unsigned int	biSize;
        int			biWidth;
        int			biHeight;
        unsigned short int  biPlanes;
        unsigned short int	biBitCount;
        unsigned int	biCompression;
        unsigned int	biSizeImage;
        int			biXPelsPerMeter;
        int			biYPelsPerMeter;
        unsigned int   biClrUsed;
        unsigned int	biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct tagRGBQUAD {
        unsigned char	rgbBlue;
        unsigned char	rgbGreen;
        unsigned char	rgbRed;
        unsigned char	rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER    bmiHeader;
    RGBQUAD             bmiColors[1];
} BITMAPINFO, *PBITMAPINFO;

typedef struct tagBITMAPFILEHEADER {
        unsigned short int	bfType;
        unsigned int	bfSize;
        unsigned short int	bfReserved1;
        unsigned short int	bfReserved2;
        unsigned int	bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

// Constants 
#define SCREEN_WIDTH  490
#define SCREEN_HEIGHT 490

#define BUTTON_FIXED_X_POS  20
#define BUTTON_FIXED_Y_POS  20
#define	X_STEP				65
#define Y_STEP				50

// 1=debug on, 0=debug off 
int gtk_debug = 0;

// Callbacks 
gint eventDelete(GtkWidget *widget, GdkEvent *event, gpointer data);
gint eventDestroy(GtkWidget *widget, GdkEvent *event, gpointer data);

void ExitCallback( GtkWidget *widget, GdkEvent *event, gpointer data);
void ScanCallback( GtkWidget *widget, GdkEvent *event, gpointer data);
void StopCallback( GtkWidget *widget, GdkEvent *event, gpointer data);
void SaveBmpCallback( GtkWidget *widget, GdkEvent *event, gpointer data);

// Images 
static GdkPixbuf *image;
static GdkPixmap *back_buffer=NULL;

// Widgets 
static GtkWidget *da;
static GtkWidget *btnScan;
static GtkWidget *btnStop;
static GtkWidget *btnSaveBmp;
static GtkWidget *lblMsg;
static GtkWidget *cboLFD;
static GtkWidget *cboLFDLevel2Strength;
static GtkWidget *chkInvert;

// Buffer to store the raw imgae 
static unsigned char *raw_data;

// Driver handle
void *hDevice;
FTRSCAN_IMAGE_SIZE ImageSize;

int bStop = FALSE;

FTR_DWORD dwLfdLevel = 0;

static pthread_t thread_id = 0;

// Expose callback for the drawing area 
static gint expose_cb (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	gdk_draw_pixmap(
			widget->window,
			widget->style->white_gc,
			back_buffer,
			event->area.x, event->area.y,
			event->area.x, event->area.y,
			event->area.width, event->area.height
	);
	return TRUE;
}

static gboolean configure_cb(GtkWidget *area, GdkEventConfigure *event)
{
	// if pixmap was already allocated - free it 
	if (back_buffer)
		gdk_pixmap_unref(back_buffer);

	// allocating back pixmap 
	back_buffer = gdk_pixmap_new(
					area->window,
					area->allocation.width,
					area->allocation.height,
					-1
	);

	if (!back_buffer)
		g_error(
			"gdk_pixmap_new(%p, %d,%d, -1) failed.\n",
			area,
			area->allocation.width,area->allocation.height
		);

	// filling all pixmap with black color 
	gdk_draw_rectangle(
				back_buffer,
				area->style->black_gc,
				TRUE,
				0,0, -1,-1
	);

	return TRUE;
}


void draw_fp_image()
{
	guchar *pixels;
	int rowstride;

	image = gdk_pixbuf_new_from_data(raw_data, GDK_COLORSPACE_RGB, FALSE, 8, ImageSize.nWidth, ImageSize.nHeight, ImageSize.nWidth*1, NULL, NULL );

	if (!image) 
	{
		if (gtk_debug) g_print( "ERROR: cannot load image into buffer.\n" );
		return;
	}
       
	rowstride = gdk_pixbuf_get_rowstride (image);
	pixels = gdk_pixbuf_get_pixels (image);

	gdk_draw_gray_image( back_buffer,
				      da->style->black_gc,
				      0, 0,
				      ImageSize.nWidth, ImageSize.nHeight,
				      GDK_RGB_DITHER_NORMAL,
				      pixels, rowstride);

	// drawing pixmap to screen 
	gdk_draw_pixmap(
			da->window,
			da->style->black_gc,
			back_buffer,
			0,0, 0,0, -1,-1
	);
}


int write_bmp_file(unsigned char *pImage, int width, int height)
{
	BITMAPINFO *pDIBHeader;
	BITMAPFILEHEADER  bmfHeader;
	int iCyc;

	// allocate memory for a DIB header
	if( (pDIBHeader = (BITMAPINFO *)malloc( sizeof( BITMAPINFO ) + sizeof( RGBQUAD ) * 255 )) == NULL )
	{
		g_print("Alloc memory failed! - Unable to write to file!!\n");
		return -1;
	}
	memset( (void *)pDIBHeader, 0, sizeof( BITMAPINFO ) + sizeof( RGBQUAD ) * 255 );
	// fill the DIB header
	pDIBHeader->bmiHeader.biSize          = sizeof( BITMAPINFOHEADER );
	pDIBHeader->bmiHeader.biWidth         = width;
	pDIBHeader->bmiHeader.biHeight        = height;
	pDIBHeader->bmiHeader.biPlanes        = 1;
	pDIBHeader->bmiHeader.biBitCount      = 8;		// 8bits gray scale bmp
	pDIBHeader->bmiHeader.biCompression   = 0;		// BI_RGB = 0;	
	// initialize logical and DIB grayscale
	for( iCyc = 0; iCyc < 256; iCyc++ )
	{
		pDIBHeader->bmiColors[iCyc].rgbBlue = pDIBHeader->bmiColors[iCyc].rgbGreen = pDIBHeader->bmiColors[iCyc].rgbRed = (unsigned char)iCyc;
	}
	// set BITMAPFILEHEADER structure
	//((char *)(&bmfHeader.bfType))[0] = 'B';
	//((char *)(&bmfHeader.bfType))[1] = 'M';
	bmfHeader.bfType = 0x42 + 0x4D * 0x100;
	bmfHeader.bfSize = 14 + sizeof( BITMAPINFO ) + sizeof( RGBQUAD ) * 255 + width * height;	//sizeof( BITMAPFILEHEADER ) = 14
	bmfHeader.bfOffBits = 14 + pDIBHeader->bmiHeader.biSize + sizeof( RGBQUAD ) * 256;
	//write to file
	FILE *fp;
	fp = fopen("frame_Ex.bmp", "wb");
	if( fp == NULL )
	{
		g_print("Failed to write to file\n");
		free( pDIBHeader );
		return -1;
	}
	//fwrite( (void *)&bmfHeader, 1, sizeof(BITMAPFILEHEADER), fp );
	fwrite( (void *)&bmfHeader.bfType, sizeof(unsigned short int), 1, fp );
	fwrite( (void *)&bmfHeader.bfSize, sizeof(unsigned int), 1, fp );
	fwrite( (void *)&bmfHeader.bfReserved1, sizeof(unsigned short int), 1, fp );
	fwrite( (void *)&bmfHeader.bfReserved2, sizeof(unsigned short int), 1, fp );
	fwrite( (void *)&bmfHeader.bfOffBits, sizeof(unsigned int), 1, fp );
	//fwrite( (void *)pDIBHeader, 1, sizeof( BITMAPINFO ) + sizeof( RGBQUAD ) * 255, fp );
	fwrite( (void *)&pDIBHeader->bmiHeader.biSize, sizeof(unsigned int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biWidth, sizeof(int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biHeight, sizeof(int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biPlanes, sizeof(unsigned short int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biBitCount, sizeof(unsigned short int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biCompression, sizeof(unsigned int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biSizeImage, sizeof(unsigned int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biXPelsPerMeter, sizeof(int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biYPelsPerMeter, sizeof(int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biClrUsed, sizeof(unsigned int), 1, fp);
	fwrite( (void *)&pDIBHeader->bmiHeader.biClrImportant, sizeof(unsigned int), 1, fp);
	for( iCyc=0; iCyc<256; iCyc++ )
	{
		fwrite( (void *)&pDIBHeader->bmiColors[iCyc].rgbBlue, sizeof(unsigned char), 1, fp );
		fwrite( (void *)&pDIBHeader->bmiColors[iCyc].rgbGreen, sizeof(unsigned char), 1, fp );
		fwrite( (void *)&pDIBHeader->bmiColors[iCyc].rgbRed, sizeof(unsigned char), 1, fp );
		fwrite( (void *)&pDIBHeader->bmiColors[iCyc].rgbReserved, sizeof(unsigned char), 1, fp );
	}
	//
   // copy fingerprint image
	unsigned char *cptrData;
	unsigned char *cptrDIBData;
	unsigned char *pDIBData;

	pDIBData = (unsigned char *)malloc( height * width);
	memset( (void *)pDIBData, 0, height * width );

	cptrData = pImage + (height - 1) * width;
	cptrDIBData = pDIBData;
	for( iCyc = 0; iCyc < height; iCyc++ )
	{
		memcpy( cptrDIBData, cptrData, width );
		cptrData = cptrData - width;
		cptrDIBData = cptrDIBData + width;
	}
	fwrite( (void *)pDIBData, 1, width * height, fp );
	fclose(fp);
	g_print("Fingerprint image is written to file: frame_Ex.bmp.\n");
	free( pDIBData );
	free( pDIBHeader );
	return 0;
}

gint eventDelete(GtkWidget *widget,GdkEvent *event,gpointer data)
{
	gtk_main_quit();
	return(0);
}

// Destroy handler for the window 
gint eventDestroy(GtkWidget *widget,GdkEvent *event,gpointer data) 
{
	bStop = TRUE;
	if( thread_id )
		pthread_join(thread_id, NULL);
	if (gtk_debug) 
		g_print( "Destroy fp_image window.\n" );
	if( hDevice )
		ftrScanCloseDevice( hDevice );
	return(0);
}

long GetTimeMS()
{
	long nTime = 0;
	struct timeval tv1;
	gettimeofday(&tv1 , NULL);
	nTime = tv1.tv_sec * 1000 + tv1.tv_usec / 1000;
	return nTime;
}

// Show the Message
void ShowMsg( unsigned long nErrCode, char *czOthers)
{
	char stError[64];

    switch( nErrCode ) 
	{
    case 0:
        strcpy( stError, "OK" );
        break;
    case FTR_ERROR_EMPTY_FRAME:	// ERROR_EMPTY
        strcpy( stError, "- Empty frame -" );
        break;
    case FTR_ERROR_MOVABLE_FINGER:
        strcpy( stError, "- Movable finger -" );
        break;
    case FTR_ERROR_NO_FRAME:
        strcpy( stError, "- No frame -" );
        break;
    case FTR_ERROR_USER_CANCELED:
        strcpy( stError, "- User canceled -" );
        break;
    case FTR_ERROR_HARDWARE_INCOMPATIBLE:
        strcpy( stError, "- Incompatible hardware -" );
        break;
    case FTR_ERROR_FIRMWARE_INCOMPATIBLE:
        strcpy( stError, "- Incompatible firmware -" );
        break;
    case FTR_ERROR_INVALID_AUTHORIZATION_CODE:
        strcpy( stError, "- Invalid authorization code -" );
        break;
    default:
        sprintf( stError, "Unknown return code - %lu", nErrCode );
	}
	strcat( stError, czOthers );
	gtk_label_set_text((GtkLabel *)lblMsg, stError);
}


void FPSetOptions(FTRHANDLE hDevice)
{
	int nIndex = gtk_combo_box_get_active( (GtkComboBox *)cboLFD );
	if( nIndex > 0 )
	{
		if( !ftrScanSetOptions( hDevice, FTR_OPTIONS_CHECK_FAKE_REPLICA,  FTR_OPTIONS_CHECK_FAKE_REPLICA ) )
		{
			ShowMsg( ftrScanGetLastError(), "" );
			return;
		}
		if( nIndex == 1 )
			dwLfdLevel = FTR_LFD_LEVEL_1;
		else if( nIndex == 2 )
			dwLfdLevel = FTR_LFD_LEVEL_2;

		FTRSCAN_PROPERTY_LFD_LEVEL LfdLevel;
		LfdLevel.dwLfdLevel = dwLfdLevel;

		if( !ftrScanSetProperty( hDevice, FTR_PROPERTY_LFD_LEVEL, &LfdLevel ) )
		{
			ShowMsg( ftrScanGetLastError(), "" );
			return;
		}
		if( dwLfdLevel == FTR_LFD_LEVEL_2 )
		{
			int nIndexStrength = gtk_combo_box_get_active( (GtkComboBox *)cboLFDLevel2Strength );
			if( nIndexStrength >= 0 )
			{
				FTRSCAN_PROPERTY_LFD_SW_1_PARAM PropertyData;
				PropertyData.dwStrength = nIndexStrength+1;	// 1-9
				if( !ftrScanSetProperty( hDevice, FTR_PROPERTY_LFD_SW_1_PARAM, &PropertyData ) )
				{
					ShowMsg( ftrScanGetLastError(), "" );
					return;
				}
			}
		}
	}
	else
		ftrScanSetOptions( hDevice, FTR_OPTIONS_CHECK_FAKE_REPLICA,  0 );

	GtkToggleButton * button = (GtkToggleButton *)chkInvert;
	if( button->active )
	{
		if( !ftrScanSetOptions( hDevice, FTR_OPTIONS_INVERT_IMAGE,  FTR_OPTIONS_INVERT_IMAGE) )
		{
				ShowMsg( ftrScanGetLastError(), "" );
				return;
		}
	}
	else
		ftrScanSetOptions( hDevice, FTR_OPTIONS_INVERT_IMAGE,  0 );
}		

// Worker thread to capture fingerprint image
static void *ScanThread( void *arg )
{
	unsigned long ulErr;
	long lT1, lT2;
	char szOthers[64]="";

	while( 1 )
	{
		if (bStop)
			break;	
	
		gdk_threads_enter();
		//
		ulErr = 0;
		memset(raw_data, 0, ImageSize.nImageSize );
		lT1 = GetTimeMS();
		if ( !ftrScanGetFrame(hDevice, raw_data, NULL) )
			ulErr = ftrScanGetLastError();
		lT2 = GetTimeMS();
		memset(szOthers, 0, 64);
		if( dwLfdLevel == FTR_LFD_LEVEL_2 )
		{
		    FTRSCAN_PROPERTY_LFD_SW_1_DATA PropertyData;
			if( ftrScanGetProperty( hDevice, FTR_PROPERTY_LFD_SW_1_CALCULATED_DATA, &PropertyData ) )
			{
				sprintf( szOthers, ". Time %ld(ms)\nCalculated: %lu", lT2-lT1, PropertyData.dwStrength );
			}
			else
				sprintf( szOthers, ". Time %ld(ms)\nCalculated:------", lT2-lT1 );
		}
		else
			sprintf( szOthers, ". Time %ld(ms)\nCalculated:------", lT2-lT1 );
		ShowMsg( ulErr, szOthers );
		draw_fp_image();
		// syncing with X server 
		gdk_flush();
		gdk_threads_leave();
		// 
		usleep(100);
	}
	pthread_exit(0);
	return NULL;
}

void ScanCallback( GtkWidget *widget, GdkEvent *event, gpointer data)
{
	if( thread_id )
	{
		bStop = TRUE;
		pthread_join(thread_id, NULL);
		thread_id = 0;
	}
	bStop = FALSE;

	FPSetOptions(hDevice);

	gtk_widget_set_sensitive(btnStop, TRUE);
	gtk_widget_set_sensitive(chkInvert, FALSE);
	gtk_widget_set_sensitive(btnSaveBmp, FALSE);
	gtk_widget_set_sensitive(btnScan, FALSE);
	gtk_widget_set_sensitive(cboLFD, FALSE);
	pthread_create(&thread_id, NULL, ScanThread, NULL);
	return;
}        

void LFDChangedCallback(GtkWidget  *item)
{
	int nIndex = gtk_combo_box_get_active( (GtkComboBox *)cboLFD );
	if( nIndex == 2 )
		gtk_widget_set_sensitive( cboLFDLevel2Strength, TRUE );
	else
		gtk_widget_set_sensitive( cboLFDLevel2Strength, FALSE );
}

void StopCallback( GtkWidget *widget, GdkEvent *event, gpointer data)
{
	if (gtk_debug) 
		g_print( "Stop button pressed.\n" );
	bStop = TRUE;
//	pthread_join(thread_id, NULL);
//	thread_id = 0;
	gtk_widget_set_sensitive(btnStop, FALSE);
	gtk_widget_set_sensitive(chkInvert, TRUE);
	gtk_widget_set_sensitive(btnScan, TRUE);
	gtk_widget_set_sensitive(btnSaveBmp, TRUE);
	gtk_widget_set_sensitive(cboLFD, TRUE);
	return;
}        

void SaveBmpCallback( GtkWidget *widget, GdkEvent *event, gpointer data)
{
	write_bmp_file( raw_data, ImageSize.nWidth, ImageSize.nHeight );
	return;
}        

void ExitCallback( GtkWidget *widget, GdkEvent *event, gpointer data)
{
	bStop = TRUE;
//	if( thread_id )
//		pthread_join(thread_id, NULL);
	usleep(2000);

	if( hDevice )
		ftrScanCloseDevice( hDevice );
	gtk_main_quit();
	return;
}        

FTR_BOOL GetDeviceInfo(FTRHANDLE hDevice, char *strInfo)
{
	FTRSCAN_DEVICE_INFO infoDevice;
	memset( &infoDevice, 0, sizeof(infoDevice) );
	infoDevice.dwStructSize = sizeof(infoDevice);
	FTR_BOOL bRet = ftrScanGetDeviceInfo( hDevice, &infoDevice );
	if( bRet )
	{
		switch( infoDevice.byDeviceCompatibility )
		{
		case FTR_DEVICE_USB_1_1:
		case FTR_DEVICE_USB_2_0_TYPE_1:
		case FTR_DEVICE_USB_2_0_TYPE_2:
		case FTR_DEVICE_USB_2_0_TYPE_80W:
			strcpy(strInfo, "FS80");
			break;
		case FTR_DEVICE_USB_2_0_TYPE_3:
			strcpy(strInfo, "FS88");
			break;
		case FTR_DEVICE_USB_2_0_TYPE_4:
		case FTR_DEVICE_USB_2_0_TYPE_90B:
			strcpy(strInfo, "FS90");
			break;
		case FTR_DEVICE_USB_2_0_TYPE_50:
			strcpy(strInfo, "FS50");
			break;
		case FTR_DEVICE_USB_2_0_TYPE_60:
			strcpy(strInfo, "FS60");
			break;
		case FTR_DEVICE_USB_2_0_TYPE_25:
			strcpy(strInfo, "FS25");
			break;
		case FTR_DEVICE_USB_2_0_TYPE_98:
			strcpy(strInfo, "FS98");
			break;
		default:
			strcpy(strInfo, "UNKNOWN Device");
			break;
		}
		//
		FTRSCAN_VERSION_INFO infoVersion;
		infoVersion.dwVersionInfoSize = sizeof( FTRSCAN_VERSION_INFO );
		bRet = ftrScanGetVersion( hDevice, &infoVersion );
		if( bRet )
		{
			char strVersion[128]="";
			sprintf(strVersion, ". ScanAPI: %d.%d.%d.%d\n", 
				infoVersion.APIVersion.wMajorVersionHi, 
				infoVersion.APIVersion.wMajorVersionLo,
				infoVersion.APIVersion.wMinorVersionHi,
				infoVersion.APIVersion.wMinorVersionLo );
			strcat(strInfo, strVersion);
			sprintf(strVersion,"Firmware: %d.%d", infoVersion.FirmwareVersion.wMajorVersionHi, infoVersion.FirmwareVersion.wMajorVersionLo);
			strcat(strInfo, strVersion);
			if( infoVersion.FirmwareVersion.wMinorVersionHi != 0xffff )
			{
				if( infoDevice.byDeviceCompatibility == FTR_DEVICE_USB_2_0_TYPE_60 || 
					infoDevice.byDeviceCompatibility == FTR_DEVICE_USB_2_0_TYPE_25 )
					sprintf(strVersion,".%c", infoVersion.FirmwareVersion.wMinorVersionHi);
				else
					sprintf(strVersion,".%d", infoVersion.FirmwareVersion.wMinorVersionHi);
				strcat(strInfo, strVersion);
			}
			if( infoVersion.FirmwareVersion.wMinorVersionLo != 0xffff )
			{
				if( infoDevice.byDeviceCompatibility == FTR_DEVICE_USB_2_0_TYPE_60 || 
					infoDevice.byDeviceCompatibility == FTR_DEVICE_USB_2_0_TYPE_25 )
					sprintf(strVersion,".%c", infoVersion.FirmwareVersion.wMinorVersionHi);
				else
					sprintf(strVersion,".%d", infoVersion.FirmwareVersion.wMinorVersionLo);
				strcat(strInfo, strVersion);
			}
			sprintf(strVersion,", Hardware: %d.%d", infoVersion.HardwareVersion.wMajorVersionHi, infoVersion.HardwareVersion.wMajorVersionLo);
			strcat(strInfo, strVersion);
			if( infoVersion.HardwareVersion.wMinorVersionHi != 0xffff )
			{
				sprintf(strVersion,".%d", infoVersion.HardwareVersion.wMinorVersionHi);
				strcat(strInfo, strVersion);
			}
			if( infoVersion.HardwareVersion.wMinorVersionLo != 0xffff )
			{
				sprintf(strVersion,".%d", infoVersion.HardwareVersion.wMinorVersionLo);
				strcat(strInfo, strVersion);
			}
		}
	}
	return bRet;
}

/* Main application */
int main(int argc,char *argv[])
{
    GtkWidget *topLevelWindow;
    GtkWidget *fixed;    
	GtkWidget *btnExit;
	GtkWidget *lbl1;

	g_thread_init(NULL);
	gdk_threads_init();
    /* Init application */
	gtk_init(&argc,&argv);
	topLevelWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(topLevelWindow), SCREEN_WIDTH, SCREEN_HEIGHT);
	gtk_window_set_title (GTK_WINDOW (topLevelWindow), "ftrScanAPI GTK Demo");	
    
    gtk_signal_connect(GTK_OBJECT(topLevelWindow),"delete_event",GTK_SIGNAL_FUNC(eventDelete),NULL);
    gtk_signal_connect(GTK_OBJECT(topLevelWindow),"destroy",GTK_SIGNAL_FUNC(eventDestroy),NULL);

	fixed = gtk_fixed_new();
	gtk_widget_show(fixed);
	// -------------------------- Label Device Info--------------------------------------------
	GtkWidget *frameInfo;
	GtkWidget *lblDevInfo;
	frameInfo = gtk_frame_new("Device Info");
	lblDevInfo=gtk_label_new("");
	gtk_widget_set_size_request ( lblDevInfo, 230, -1 );
	gtk_container_add(GTK_CONTAINER(frameInfo), lblDevInfo);
	gtk_fixed_put(GTK_FIXED(fixed),frameInfo, BUTTON_FIXED_X_POS, BUTTON_FIXED_Y_POS );    
	// -------------------------- Checkbox InvertImage ----------------------------------------------
    chkInvert = gtk_check_button_new_with_label(" Invert ");
    gtk_fixed_put(GTK_FIXED(fixed),chkInvert, BUTTON_FIXED_X_POS, (BUTTON_FIXED_Y_POS+Y_STEP+15) );    
	// -------------------------- ComboBox LFD ----------------------------------------------
	lbl1 = gtk_label_new("LFD");
	gtk_fixed_put(GTK_FIXED(fixed), lbl1, BUTTON_FIXED_X_POS, (BUTTON_FIXED_Y_POS+2*Y_STEP+5) );   
	cboLFD = gtk_combo_box_new_text();
	gtk_combo_box_append_text( (GtkComboBox *)cboLFD, "off");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFD, "Level 1");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFD, "Level 2");
	gtk_fixed_put(GTK_FIXED(fixed),cboLFD, BUTTON_FIXED_X_POS+X_STEP, (BUTTON_FIXED_Y_POS+2*Y_STEP) );    
	gtk_combo_box_set_active( (GtkComboBox *)cboLFD, 0 );
	gtk_signal_connect(GTK_OBJECT(cboLFD), "changed", GTK_SIGNAL_FUNC(LFDChangedCallback), NULL);
	// -------------------------- ComboBox LFD Level2 Strength ----------------------------------------------
	lbl1 = gtk_label_new("LFD Level2 Strength");
	gtk_fixed_put(GTK_FIXED(fixed), lbl1, BUTTON_FIXED_X_POS, (BUTTON_FIXED_Y_POS+3*Y_STEP+5) );   
	cboLFDLevel2Strength = gtk_combo_box_new_text();
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "1");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "2");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "3");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "4");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "5");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "6");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "7");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "8");
	gtk_combo_box_append_text( (GtkComboBox *)cboLFDLevel2Strength, "9");
	gtk_fixed_put(GTK_FIXED(fixed),cboLFDLevel2Strength, BUTTON_FIXED_X_POS+X_STEP*2+10, (BUTTON_FIXED_Y_POS+3*Y_STEP) );    
	gtk_combo_box_set_active( (GtkComboBox *)cboLFDLevel2Strength, 0 );
	gtk_widget_set_sensitive( cboLFDLevel2Strength, FALSE );
	// -------------------------- Scan button -------------------------------------------------
    btnScan = gtk_button_new_with_label("  Scan  ");
    gtk_signal_connect(GTK_OBJECT(btnScan), "clicked", GTK_SIGNAL_FUNC(ScanCallback), NULL);
    gtk_fixed_put(GTK_FIXED(fixed),btnScan, BUTTON_FIXED_X_POS,(BUTTON_FIXED_Y_POS+Y_STEP*4) );    
	// -------------------------- Stop button --------------------------------------------------
    btnStop = gtk_button_new_with_label("  Stop  ");
    gtk_signal_connect(GTK_OBJECT(btnStop), "clicked", GTK_SIGNAL_FUNC(StopCallback), NULL);
	gtk_widget_set_sensitive(btnStop, FALSE);
    gtk_fixed_put(GTK_FIXED(fixed),btnStop, BUTTON_FIXED_X_POS+X_STEP,(BUTTON_FIXED_Y_POS+4*Y_STEP) );    
	// -------------------------- Label Message -------------------------------------------------
	lblMsg=gtk_label_new("Press Scan button");
    gtk_fixed_put(GTK_FIXED(fixed),lblMsg, BUTTON_FIXED_X_POS,(BUTTON_FIXED_Y_POS+5*Y_STEP) );    
	// ------------------------- SaveBmp button -------------------------------------------------
    btnSaveBmp = gtk_button_new_with_label("  Save  ");
    gtk_signal_connect(GTK_OBJECT(btnSaveBmp), "clicked", GTK_SIGNAL_FUNC(SaveBmpCallback), NULL);
	gtk_widget_set_sensitive(btnSaveBmp, FALSE);
    gtk_fixed_put(GTK_FIXED(fixed),btnSaveBmp, BUTTON_FIXED_X_POS,(BUTTON_FIXED_Y_POS+6*Y_STEP) );    
	// -------------------------- Exit button ----------------------------------------------------
    btnExit = gtk_button_new_with_label("  Exit  ");
    gtk_signal_connect(GTK_OBJECT(btnExit), "clicked", GTK_SIGNAL_FUNC(ExitCallback), NULL);
    gtk_fixed_put(GTK_FIXED(fixed),btnExit, BUTTON_FIXED_X_POS, (BUTTON_FIXED_Y_POS+7*Y_STEP) );    
								 	
	// -------------------------- Init the libftrScanAPI library ----------------------------------
	hDevice = ftrScanOpenDevice();
	if( hDevice == NULL )
	{
		g_print( "Failed to open device!\n" );
		return -1;
	} 
	char strDevInfo[128];
	if( GetDeviceInfo(hDevice, strDevInfo ) )
	{
		gtk_label_set_text((GtkLabel *)lblDevInfo, strDevInfo);
	}
	else
	{
		g_print( "Failed to open device!\n" );
		ftrScanCloseDevice(hDevice);
		return -1;
	}
	if( !ftrScanGetImageSize( hDevice, &ImageSize ) )
	{
		g_print("Failed to get image size\n");
		ftrScanCloseDevice( hDevice );
		return -1;
	}
	raw_data = (unsigned char *)malloc( ImageSize.nImageSize );
 	memset(raw_data, 0, ImageSize.nImageSize );
    // ------------------------- Add drawing area to display the fingerprint image -------------------
	da = gtk_drawing_area_new ();
	gtk_signal_connect(	(GtkObject*)da,	"expose_event",	(GtkSignalFunc)expose_cb, NULL );
	gtk_signal_connect( (GtkObject*)da, "configure_event", (GtkSignalFunc)configure_cb, NULL );
	gtk_drawing_area_size((GtkDrawingArea*)da, ImageSize.nWidth, ImageSize.nHeight);
	gtk_fixed_put(GTK_FIXED(fixed),da, BUTTON_FIXED_X_POS+4*X_STEP, 5 );    

	//-------------------------- Display the widgets --------------------------------------------------
    gtk_container_add(GTK_CONTAINER(topLevelWindow), fixed);
	//gtk_widget_show(topLevelWindow);
	gtk_widget_show_all(topLevelWindow);

    //-------------------------- Main application loop ------------------------------------------------
	gdk_threads_enter();
    gtk_main();
	gdk_threads_leave();
	//
	if (gtk_debug) g_print("Done!\n");	
    exit(0);
}

