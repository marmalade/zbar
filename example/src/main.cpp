#include "IwGx.h"
#include "Iw2D.h"
#include "IwNUI.h"
using namespace IwNUI;

#include "s3eTimer.h"
#include "s3eCamera.h"
#include "zbar.h"

//Function prototypes
void RequestQuit();
void StartCamera();
void StopCamera();
int32 CameraUpdateCallback(void*, void*);
int32 CameraStoppedCallback(void*, void*);
int32 ScanQrCodeCallback(void*, void*);

enum CameraState {
	CAMERA_IDLE, //Camera has been stopped or has not yet been started
	CAMERA_LOADING, // Waiting for first frame from camera
	CAMERA_STREAMING, // Actively receiving frames from camera
	CAMERA_UNAVAILABLE // No camera, camera is in use by another app, camera doesn't support the requested format, or camera error
};
CameraState g_CameraState = CAMERA_IDLE;

//Camera
uint16* g_pCameraTexelsRGB565 = NULL; //Buffer to hold cropped raw camera pixels in RGB565 format. These pixels are displayed on screen.
uint g_frameResolution, g_frameRotation = 0;
uint g_cameraSquareDimension = 0; //The dimension of the square cropped out of the raw camera preview data (frameData). 
uint g_cameraCropXStart, g_cameraCropYStart = 0; //The coordinates of the top left corner of the cropping square inside of the raw camera preview data.
CIwTexture* g_pCameraTextureRGB565 = NULL; //This texture uses the data held in the g_pCameraTexelsRGB565 buffer.

//ZBar
bool g_qrCodeFound = false;
uint8* g_pCameraPixelsGrayscale = NULL; //Buffer to hold grayscale converted camera pixels (RGB565 -> YUV800). These pixels are scanned by ZBar for QR codes
zbar_image_scanner_t* g_pZBarScanner = NULL; //Zbar object that process images for barcodes
zbar_image_t* g_pZBarImage = NULL; //Zbar object returned by g_pZBarScanner containing any identified barcodes
uint g_qrScanTimeout = 1000; //The period between each QR code scan
char* g_pQrString = NULL; //The identified text within the QR code

struct MyNUIElements {
	CLabelPtr pTextStatus;
	CButtonPtr pBtnScan;
};
MyNUIElements* g_myNUIElements;

//¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,
//FUNCTIONS

bool OnButtonClick(void* pData, CButton* pBtnNUI) {
	
	//Get button name
	CString btnName;
	if(!(pBtnNUI->GetAttribute("name", btnName))) {
		IwTrace(]-->,("Failed to get button name."));
		s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Failed to get button name.");
		return false;
	}
	const char* btnNameStr = btnName.Get();
	//IwTrace(]-->,("btnName = %s", btnNameStr));
	
	//Check which button was pressed.
	if(strcmp("btnScanAgain", btnNameStr) == 0) {
		IwTrace(]-->,("Scan Again button pressed."));
		g_qrCodeFound = false;
		g_myNUIElements->pBtnScan->SetAttribute("enabled", "0");
		g_myNUIElements->pTextStatus->SetAttribute("caption", "Scanning for QR Code...");
	}
	else if(strcmp("btnQuit", btnNameStr) == 0) {
		IwTrace(]-->,("Quit button pressed."));
		RequestQuit();
	}
	else {
		IwTrace(]-->,("No match found for button click!"));
		s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "No match found for button click!");
	}

	return true;
}

void RequestQuit() {
	if(g_CameraState == CAMERA_LOADING || g_CameraState == CAMERA_STREAMING)
		StopCamera();
	s3eDeviceRequestQuit();
}

//Start camera, trace camera info, register camera callbacks and create ZBarScanner object.
void StartCamera() {
	//Check if camera is available
	if(s3eCameraAvailable() != S3E_TRUE) {
		IwTrace(]-->, ("Camera not available"));
		s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Camera not available.");
		g_myNUIElements->pTextStatus->SetAttribute("caption", "Camera not available!");
		g_CameraState = CAMERA_UNAVAILABLE;
		return;
	}
	else {
		IwTrace(]-->, ("Camera available"));

		//Check and trace which pixel formats are supported (RGB565_CONVERTED will be used regardless)
		s3eBool cameraPixelRGB565 = s3eCameraIsFormatSupported(S3E_CAMERA_PIXEL_TYPE_RGB565);
		s3eBool cameraPixelRGB888 = s3eCameraIsFormatSupported(S3E_CAMERA_PIXEL_TYPE_RGB888);
		s3eBool cameraPixelNV21 = s3eCameraIsFormatSupported(S3E_CAMERA_PIXEL_TYPE_NV21);
		s3eBool cameraPixelNV12 = s3eCameraIsFormatSupported(S3E_CAMERA_PIXEL_TYPE_NV12);
		s3eBool cameraPixelBGRA8888 = s3eCameraIsFormatSupported(S3E_CAMERA_PIXEL_TYPE_BGRA8888);
					
		//Trace the status of supported pixel formats
		IwTrace(]-->, ("Camera pixel format RGB565 supported = %u", cameraPixelRGB565));
		IwTrace(]-->, ("Camera pixel format RGB888 supported = %u", cameraPixelRGB888));
		IwTrace(]-->, ("Camera pixel format NV21 supported = %u", cameraPixelNV21));
		IwTrace(]-->, ("Camera pixel format NV12 supported = %u", cameraPixelNV12));
		IwTrace(]-->, ("Camera pixel format BGRA8888 supported = %u", cameraPixelBGRA8888));

		//Start camera
		if(s3eCameraStart(S3E_CAMERA_STREAMING_SIZE_HINT_MEDIUM, S3E_CAMERA_PIXEL_TYPE_RGB565_CONVERTED) != S3E_RESULT_SUCCESS) {
			IwTrace(]-->, ("Start camera failed"));
			///s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Error starting camera.");
			g_CameraState = CAMERA_UNAVAILABLE;
			return;
		}
		g_CameraState = CAMERA_LOADING;
		IwTrace(]-->, ("Start camera successful"));
			
		//Register camera update callback
		if(s3eCameraRegister(S3E_CAMERA_UPDATE_STREAMING, CameraUpdateCallback, NULL) != S3E_RESULT_SUCCESS) {
			IwTrace(]-->, ("CameraUpdateCallback register failed"));
			s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Error registering camera.");
			StopCamera();
			g_CameraState = CAMERA_UNAVAILABLE;
			return;
		}

		//Register camera stopped callback
		if(s3eCameraRegister(S3E_CAMERA_STOP_STREAMING, CameraStoppedCallback, NULL) != S3E_RESULT_SUCCESS) {
			IwTrace(]-->, ("CameraStoppedCallback register failed"));
			s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Error registering camera.");
			StopCamera();
			g_CameraState = CAMERA_UNAVAILABLE;
			return;
		}

		//Creat ZBarScanner object
		g_pZBarScanner = zbar_image_scanner_create();
		if (g_pZBarScanner == NULL) {
			IwTrace(]-->, ("Create ZBar image scanner failed"));
			s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Failed to initialize ZBar.");
			StopCamera();
			return;
		}
		zbar_image_scanner_set_config(g_pZBarScanner, ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
	}
}

//Unregisters camera callbacks, frees allocated buffers and deletes created camera/zbar objects.
void StopCamera() {
	//Unregister camera callbacks, stop camera
	s3eCameraUnRegister(S3E_CAMERA_UPDATE_STREAMING, CameraUpdateCallback);
	s3eCameraUnRegister(S3E_CAMERA_STOP_STREAMING, CameraStoppedCallback);
	s3eCameraStop();
	g_CameraState = CAMERA_IDLE;

	//Free memory buffers
	s3eFree(g_pCameraTexelsRGB565);
	s3eFree(g_pCameraPixelsGrayscale);
	g_pCameraTexelsRGB565 = NULL;
	g_pCameraPixelsGrayscale = NULL;
	
	//Delete created objects
	if (g_pCameraTextureRGB565 != NULL) {
		delete g_pCameraTextureRGB565;
		g_pCameraTextureRGB565 = NULL;
	}
	if (g_pZBarImage) {
		zbar_image_destroy(g_pZBarImage);
		g_pZBarImage = NULL;
	}
	if (g_pZBarScanner) {
		zbar_image_scanner_destroy(g_pZBarScanner);
		g_pZBarScanner = NULL;
	}
	
	IwTrace(]-->, ("Stop camera successful"));
}

//¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,
//CALLBACKS

//Callback called every time a camera preview frame is ready.
//Information about the frame is extracted and buffers to copy the image data are (re)allocated if needed.
//The camera preview frame rotated if needed during the copy and is finally uploaded to VRAM for rendering
int32 CameraUpdateCallback(void* eventData, void* userData) {
	if(g_CameraState == CAMERA_LOADING) { //First frame has now been received. Update CameraState.
		g_CameraState = CAMERA_STREAMING;
		g_myNUIElements->pTextStatus->SetAttribute("caption", "Scanning for QR Code...");
		s3eTimerSetTimer(g_qrScanTimeout, ScanQrCodeCallback, 0);
	}
	else if(g_CameraState != CAMERA_STREAMING)	//CameraState is either CAMERA_IDLE or CAMERA_UNAVAIALBE. Do not process CameraFrameData.
		return 0;
		
	//Cast the callback's eventData to s3eCameraFrameData* type and extract some of its properties.
	s3eCameraFrameData* CameraFrameData = (s3eCameraFrameData*)eventData;
	uint frameWidth = CameraFrameData->m_Width;
	uint frameHeight = CameraFrameData->m_Height;
	uint frameResolution = frameWidth * frameHeight;
	uint framePitch = CameraFrameData->m_Pitch;
	uint frameRotation = CameraFrameData->m_Rotation;
	uint16* frameData = (uint16*)CameraFrameData->m_Data;

	//Check if the camera pixel buffer has not been allocated or if the CameraFrameData has changed.
	if(g_pCameraTexelsRGB565 == NULL ||
		frameResolution != g_frameResolution||
		frameRotation != g_frameRotation) {
		//Check the pixel format of the CameraFrameData.
		if(CameraFrameData->m_PixelType != S3E_CAMERA_PIXEL_TYPE_RGB565_CONVERTED) {
			IwTrace(]-->, ("CameraFrameData is not in RGB565 format."));
			s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Camera pixel format error.");
			StopCamera();
			g_CameraState = CAMERA_UNAVAILABLE;
			return 0;
		}
		
		//Trace the properties of the CameraFrameData.
		IwTrace(]-->, ("Camera raw preview width = %u", frameWidth));
		IwTrace(]-->, ("Camera raw preview height = %u", frameHeight));
		IwTrace(]-->, ("Camera raw preview pitch = %u", framePitch));
		IwTrace(]-->, ("Camera raw preview rotation = %u", frameRotation));

		//Calculate the the cropping dimensions depending on raw camera aspect ratio < or > 1.
		if(frameWidth > frameHeight) {
			g_cameraSquareDimension = frameHeight;
			g_cameraCropXStart = (frameWidth - frameHeight) / 2;
			g_cameraCropYStart = 0;
		}
		else {
			g_cameraSquareDimension = frameWidth;
			g_cameraCropXStart = 0;
			g_cameraCropYStart = (frameHeight - frameWidth) / 2;
		}

		//Calculate buffer sizes and (re)allocate the buffers.
		uint cameraGrayscaleBufferSize = g_cameraSquareDimension * g_cameraSquareDimension; //Size in bytes
		uint cameraRGB565BufferSize = cameraGrayscaleBufferSize * 2;	//Size in bytes
		g_pCameraTexelsRGB565 = (uint16*) s3eRealloc(g_pCameraTexelsRGB565, cameraRGB565BufferSize);
		g_pCameraPixelsGrayscale = (uint8*) s3eRealloc(g_pCameraPixelsGrayscale, cameraGrayscaleBufferSize);
		if(g_pCameraTexelsRGB565 == NULL || g_pCameraPixelsGrayscale == NULL) {
			IwTrace(]-->, ("Not enough memory for camera preview buffers"));
			s3eDebugErrorShow(S3E_MESSAGE_CONTINUE, "Not enough memory for camera.");
			StopCamera();
			return 0;
		}

		//Create a RGB565 texture using the RGB565 buffer for efficient rendering.
		if (g_pCameraTextureRGB565 != NULL)
			delete g_pCameraTextureRGB565;
		g_pCameraTextureRGB565 = new CIwTexture;
		g_pCameraTextureRGB565->SetModifiable(true);
		g_pCameraTextureRGB565->SetMipMapping(false);
		g_pCameraTextureRGB565->CopyFromBuffer(g_cameraSquareDimension, g_cameraSquareDimension,
			CIwImage::RGB_565, g_cameraSquareDimension<<1, (uint8*)g_pCameraTexelsRGB565, NULL);
		
		// Initialize ZBar Image object:
		if (g_pZBarScanner) {
			if (g_pZBarImage)
				zbar_image_destroy(g_pZBarImage);
			g_pZBarImage = zbar_image_create();
			if (g_pZBarImage) {
				zbar_image_set_format(g_pZBarImage, *(unsigned long*)"Y800");
				zbar_image_set_size(g_pZBarImage, g_cameraSquareDimension, g_cameraSquareDimension);
				zbar_image_set_data(g_pZBarImage, g_pCameraPixelsGrayscale, cameraGrayscaleBufferSize, NULL);
			}
		}
	}
	
	//Copy current values to global variables (needed for above if statement)
	g_frameResolution = frameResolution;
	g_frameRotation = frameRotation;

	//Rotate and crop raw frameData buffer to g_pCameraTexelsRGB565 buffer. 
	//The dest pointer (g_pCameraTexelsRGB565) is always copied to in the way a book is read (line by line).
	//The src pointer (frameData) is copied from in a manner that accomplishes rotation (not necessarily like a book).
	if(g_pCameraTextureRGB565) {
		if(!g_qrCodeFound) { //Don't update the preview if a QR code was found.
			uint16* src = &frameData[g_cameraCropXStart + (g_cameraCropYStart * frameWidth)]; //Source pointer adjusted for cropping
			uint16* dest = g_pCameraTexelsRGB565; //Target destination pointer
		
			switch(frameRotation) {
				case S3E_CAMERA_FRAME_ROTNORMAL: { //Perform no rotation. Just copy cropped area.
					//Constant to adjust src 1 pixel to the down, g_cameraSquareDimension pixels left
					const uint srcDelta = frameWidth - g_cameraSquareDimension; 

					for(uint i = g_cameraSquareDimension; i; --i, src += srcDelta)
						for(uint j = g_cameraSquareDimension; j; --j)
							*dest++ = *src++;
					break;
				}
				case S3E_CAMERA_FRAME_ROT90: { //Rotate 90 degrees CCW to correct orientation.
					//Constant to adjust src 1 pixel to the left, g_cameraSquareDimension pixels up (by subtraction)
					const uint srcDelta = 1 + g_cameraSquareDimension * frameWidth; 

					//Start src at top right corner. Copy path is all the way down, then one to the left, all the way down, one to the left, etc.
					src += g_cameraSquareDimension - 1; 

					for(uint i = g_cameraSquareDimension; i; --i, src -= srcDelta) 
						for(uint j = g_cameraSquareDimension; j; --j, src += frameWidth)
							*dest++ = *src;
					break;
				}
				case S3E_CAMERA_FRAME_ROT180: { //Rotate 180 degrees to correct orientation.
					//Constant to adjust src 1 pixel to the up, g_cameraSquareDimension pixels to the right (by subtraction)
					const uint srcDelta = frameWidth - g_cameraSquareDimension;

					//Start src at bottom right corner. Copy path is all the way to the left, then one up, all the way left, one up, etc.
					src += g_cameraSquareDimension * frameWidth - srcDelta;
				
					for(uint i = g_cameraSquareDimension; i; --i, src -= srcDelta) 
						for(uint j = g_cameraSquareDimension; j; --j)
							*dest++ = *src--;
					break;
				}
				case S3E_CAMERA_FRAME_ROT270: { //Rotate 90 degrees CW to correct orientation.
					//Constant to adjust src 1 pixel to the right, g_cameraSquareDimension pixels down
					const uint srcDelta = 1 + g_cameraSquareDimension * frameWidth;

					//Start src at bottom left corner. Copy path is all the way up, then one to the right, all the way up, one to the right, etc.
					src += (g_cameraSquareDimension - 1) * frameWidth;

					for(uint i = g_cameraSquareDimension; i; --i, src += srcDelta) 
						for(uint j = g_cameraSquareDimension; j; --j, src -= frameWidth)
							*dest++ = *src;
					break;
				}
			}
		}
		
		//Update the hardware texture buffer:
		g_pCameraTextureRGB565->ChangeTexels((uint8*)g_pCameraTexelsRGB565, CIwImage::RGB_565); //Not sure why this is required. Same buffer every time...
		g_pCameraTextureRGB565->Upload();
	}
	return 0;
}

//This callback is called if the camera preview is interrupted for any unexpected reason (e.g. phone call, batter dead etc.)
int32 CameraStoppedCallback(void* eventData, void* userData) {
	StopCamera();
	return 0;
}

//This callback is periodically called to scan the camera preview frame for QR codes.
//It is necessary to convert the RGB565 camera preview pixels to YUV800 (grayscale) in order for ZBar to process the data
int32 ScanQrCodeCallback(void* systemData, void* userData) {
	if(!g_qrCodeFound && g_CameraState == CAMERA_STREAMING && g_pZBarImage) {
		//Convert the RGB565 camera preview pixels to YUV800 (grayscale)
		uint numPixels = g_cameraSquareDimension * g_cameraSquareDimension;
		uint16* src = g_pCameraTexelsRGB565;
		uint8* dest = g_pCameraPixelsGrayscale;

		for(uint i = 0; i < numPixels; ++i, ++src) {
			const uint8
				r = ((*src)&0xf800)>>8,
				g = ((*src)&0x07e0)>>3,
				b = ((*src)&0x001f)<<3;
			const int yuv800 = (77*r + 150*g + 29*b) >> 8;
			*dest++ = (yuv800>255) ? 255 : ((yuv800<0) ? 0 : (uint8)yuv800);
		}
		
		//Scan the grayscale converted image for QR codes
		if(zbar_scan_image(g_pZBarScanner, g_pZBarImage)) {
			const zbar_symbol_t *symbol = zbar_image_first_symbol(g_pZBarImage);

			//Iterate through all symbols within the ZBar Image object
			for(; symbol; symbol = zbar_symbol_next(symbol)) {
				if (zbar_symbol_get_type(symbol) == ZBAR_QRCODE) { //Extract and print the QR code text
					g_qrCodeFound = true;
					IwTrace(]-->, ("QR code found!"));
					const char* pQrData = zbar_symbol_get_data(symbol);
					const unsigned int qrDataLength = zbar_symbol_get_data_length(symbol);
					char qrString[512];
					strcpy(qrString, "QR Code found: ");
					strcat(qrString, pQrData);
					g_myNUIElements->pTextStatus->SetAttribute("caption", qrString);
					g_myNUIElements->pBtnScan->SetAttribute("enabled", "1");
					
					IwTrace(]-->, ("pQrData = %s", pQrData));
					IwTrace(]-->, ("qrDataLength = %u", qrDataLength));
				}
			}
		}

	}
	s3eTimerSetTimer(g_qrScanTimeout, ScanQrCodeCallback, 0); //Set the timer again as it only runs once
	return 0;
}



//¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,
//¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,¸¸¸¸¸¸¸,¤°``°¤ø,¸¸¸¸,¤°``°¤ø,
//MAIN FUNCTION

int main() {
	IwTrace(]-->, ("ZBar Marmalade Demo App Started!"));
	
	//Initialize Iw2D
	Iw2DInit();
	
    //Initialize Native UI
	CAppPtr pApp = CreateApp(); //Manages NUI start-up, shut-down and windows
	CWindowPtr pWindow = CreateWindow(); //Window is the root of the view hierarchy (only 1 active at a time)
    pApp->AddWindow(pWindow); //pApp will now manage pWindow
	
	//Get screen dimens (surface orientation is forced to portrait)
	const int screenW = IwGxGetScreenWidth();
	const int screenH = IwGxGetScreenHeight();
	const CIwSVec2 screen = CIwSVec2(IwGxGetScreenWidth(), IwGxGetScreenHeight());

	const char* defaultFont = "Serif";

	//Determine default font sizes for IwNUI elements based on screen dimens
	const char* defaultFontSize; //medium for HVGA, large for WVGA/iPhone (x-large maybe better but produces IwGxFont memory overload)
	const char* defaultFontSizeSmall;
	if(screenW < 480 || screenH < 800) {
		defaultFontSize = "medium"; 
		defaultFontSizeSmall = "x-small";
	}
	else {
		defaultFontSize = "large"; 
		defaultFontSizeSmall = "small";
	}

	//Create Native UI elements
	g_myNUIElements = new MyNUIElements; //Holds references to pBtnScan and pTextStatus

	//Header text "Marmalade Barcode Scanner Demo"
	CLabelPtr pTextHeader = CreateLabel(CAttributes()
		.Set("name",		"textHeader")
		.Set("font",		defaultFont) //Does nothing
		.Set("fontSize",	defaultFontSize)
		.Set("fontAlignH",	"centre")	.Set("fontAlignV",	"centre")
		.Set("x1",			"50%")		.Set("y1",		"2%")
		//.Set("width",		"auto")		.Set("height",	"auto") //Default is "auto"
		.Set("alignW",		"centre")	.Set("alignH",	"top")
		.Set("caption",		"Marmalade QR Scanner Demo")
	);

	//"Scan Again" button
	g_myNUIElements->pBtnScan = CreateButton(CAttributes()
		.Set("name",		"btnScanAgain")
		.Set("font",		defaultFont) //Does nothing
		.Set("fontSize",	defaultFontSize)
		.Set("x1",			"50%")		.Set("y1",		"10%")
		.Set("width",		"96%")		.Set("height",	"10%")
		.Set("alignW",		"centre")	.Set("alignH",	"top")
		.Set("caption",		"Scan Again")
		.Set("enabled",		"0")
	);
	g_myNUIElements->pBtnScan->SetEventHandler("click", (void*)NULL, &OnButtonClick);

	//Status text
	//Determine Y1 based on screen aspect ratio
	uint textStatusY1 = 22 + (uint)(94 * ((float)screenW / (float)screenH));
	char textStatusY1buffer[8];
	sprintf(textStatusY1buffer, "%u", textStatusY1);
	strcat(textStatusY1buffer, "%");
	const char* textStatusY1str = textStatusY1buffer;

	g_myNUIElements->pTextStatus = CreateLabel(CAttributes()
		.Set("name",		"textZBar")
		.Set("font",		defaultFont) //Does nothing
		.Set("fontSize",	defaultFontSizeSmall)
		.Set("fontAlignH",	"centre")	.Set("fontAlignV",	"centre")
		.Set("x1",			"50%")		.Set("y1",		textStatusY1str)
		//.Set("width",		"auto")		.Set("height",	"auto") //Default is "auto"
		.Set("alignW",		"centre")	.Set("alignH",	"top")
		.Set("caption",		"Starting camera...")
		//.Set("caption",		textStatusY1str)
	);

	//Footer text "Powered by ZBar"
	CLabelPtr pTextZBar = CreateLabel(CAttributes()
		.Set("name",		"textZBar")
		.Set("font",		defaultFont) //Does nothing
		.Set("fontSize",	defaultFontSize)
		.Set("fontAlignH",	"left")		.Set("fontAlignV",	"bottom")
		.Set("x1",			"3%")		.Set("y2",			"1%")
		//.Set("width",		"auto")		.Set("height",		"auto") //Default is "auto"
		.Set("alignW",		"left")		.Set("alignH",		"bottom")
		.Set("caption",	"Powered by ZBar")
	);

	//"Quit" button
	CButtonPtr pBtnQuit = CreateButton(CAttributes()
		.Set("name",		"btnQuit")
		.Set("font",		defaultFont) //Does nothing
		.Set("fontSize",	defaultFontSize)
		.Set("x2",			"2%")		.Set("y2",		"1%")
		.Set("width",		"30%")		.Set("height",	"10%")
		.Set("alignW",		"right")	.Set("alignH",	"bottom")
		.Set("caption",		"Quit")
	);
	pBtnQuit->SetEventHandler("click", (void*)NULL, &OnButtonClick);
	
	//Create root element and add all child elements to the view
	CViewPtr pView = CreateView("canvas"); //pView is the root element of view hierarchy
	pView->AddChild(pTextHeader);
	pView->AddChild(g_myNUIElements->pBtnScan);
	pView->AddChild(g_myNUIElements->pTextStatus);
	pView->AddChild(pTextZBar);
	pView->AddChild(pBtnQuit);
	
	//Attach pView->pWindow and finally show pWindow
	pWindow->SetChild(pView);
    pApp->ShowWindow(pWindow);
    //pApp->Run(); //Begin NUI loop: pApp->Update() in main loop is used instead
	
	//Define red square dimensions
	const CIwSVec2 cameraPreviewWH = CIwSVec2((const int)(screenW * 0.94), (const int)(screenW * 0.94));
	const CIwSVec2 cameraPreviewXY = CIwSVec2((const int)(screenW * 0.03), (const int)(screenH * 0.22));

	//Set colors
    IwGxSetColClear(0xff, 0xff, 0xff, 0xff); //Set IwGx to white
	Iw2DSetColour(0xFF000088); // Set Iw2D to red

	bool cameraInitialized = false;

	while(!s3eDeviceCheckQuitRequest()) {
		s3eKeyboardUpdate();

		// Clear the surface
		IwGxClear(IW_GX_COLOUR_BUFFER_F | IW_GX_DEPTH_BUFFER_F);

		//Render the camera preview
		if(g_CameraState == CAMERA_STREAMING) { //Draw the camera preview
			CIwMaterial* pMaterial = IW_GX_ALLOC_MATERIAL();
			pMaterial->SetTexture(g_pCameraTextureRGB565);
			pMaterial->SetColAmbient(0xffffffff);
			IwGxSetMaterial(pMaterial);
			IwGxDrawRectScreenSpace(&cameraPreviewXY, &cameraPreviewWH);
		}
		else //Draw a red rectangle
			Iw2DFillRect(cameraPreviewXY, cameraPreviewWH);

		//Update the Native UI
		pApp->Update(); //Calls IwGxFlush() & IwGxSwapBuffers() within
		
		//Allow NUI to render once before starting camera
		if(!cameraInitialized) {
			StartCamera();
			cameraInitialized = true;
		}
		
		s3eDeviceYield();
	}

	delete g_myNUIElements;

	Iw2DTerminate();

	return 0;
}
