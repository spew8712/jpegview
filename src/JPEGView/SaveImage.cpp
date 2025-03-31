#include "StdAfx.h"
#include "SaveImage.h"
#include "JPEGImage.h"
#include "BasicProcessing.h"
#include "Helpers.h"
#include "SettingsProvider.h"
#include "ParameterDB.h"
#include "EXIFReader.h"
#include "TJPEGWrapper.h"
#include "WEBPWrapper.h"
#include "QOIWrapper.h"
#include <gdiplus.h>

//////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////////////////////////

// Removes an existing comment segment (if any) and returns the new JPEG stream, NULL if no comment was found in stream
static uint8* RemoveExistingCommentSegment(void* pJPEGStream, int& nStreamLength) {
	uint8* pCommentSeg = (uint8*)Helpers::FindJPEGMarker(pJPEGStream, nStreamLength, 0xFE);
	if (pCommentSeg != NULL) {
		int nLenSegment = pCommentSeg[2] * 256 + pCommentSeg[3];
		int nHeader = (int)(pCommentSeg - (uint8*)pJPEGStream);
		uint8* pNewBuffer = new uint8[nStreamLength - nLenSegment];
		memcpy(pNewBuffer, pJPEGStream, nHeader);
		memcpy(&(pNewBuffer[nHeader]), pCommentSeg + nLenSegment, nStreamLength - nHeader - nLenSegment);
		nStreamLength = nStreamLength - nLenSegment;
		return pNewBuffer;
	}
	return NULL;
}

// Finds first JPEG marker, returns NULL if none found (invalid JPEG stream?)
static uint8* FindFirstJPEGMarker(void* pJPEGStream, int nStreamLength) {
	uint8* pStream = (uint8*)pJPEGStream;
	if (pStream == NULL || nStreamLength < 3 || pStream[0] != 0xFF || pStream[1] != 0xD8) {
		return NULL; // not a JPEG
	}
	int nIndex = 2;
	do {
		if (pStream[nIndex] == 0xFF) {
			// block header found, skip padding bytes
			while (pStream[nIndex] == 0xFF && nIndex < nStreamLength) nIndex++;
			break;
		}
		else {
			break; // block with pixel data found, start hashing from here
		}
	} while (nIndex < nStreamLength);

	if (pStream[nIndex] != 0) {
		return &(pStream[nIndex - 1]); // place on marker start
	}
	else {
		return NULL;
	}
}

// Inserts the specified string as a JPEG comment into the JPEG stream. Returns NULL in case of errors, a new JPEG stream otherwise
// The string is written as UTF-8
static uint8* InsertCommentBlock(void* pJPEGStream, int& nStreamLength, LPCTSTR comment) {
	const int MAX_COMMENT_LEN = 4096;
	uint8* pFirstJPGMarker = FindFirstJPEGMarker(pJPEGStream, nStreamLength);
	if (pFirstJPGMarker == NULL) {
		return NULL;
	}

	int numberOfCharsToConvert = min(MAX_COMMENT_LEN, (int)_tcslen(comment));
	int utf8Length = ::WideCharToMultiByte(CP_UTF8, 0, comment, numberOfCharsToConvert, NULL, 0, NULL, NULL);
	if (utf8Length == 0) {
		return NULL;
	}

	char* pBuffer = new char[utf8Length];
	::WideCharToMultiByte(CP_UTF8, 0, comment, numberOfCharsToConvert, pBuffer, utf8Length, 0, 0);

	uint8* pNewStream = new uint8[nStreamLength + utf8Length + 4]; // +4: two bytes marker, two bytes segment length
	
	if (pFirstJPGMarker[1] == 0xE0) {
		int nApp0Len = pFirstJPGMarker[2] * 256 + pFirstJPGMarker[3];
		pFirstJPGMarker += 2;
		pFirstJPGMarker += nApp0Len;
	}

	int nSizeToMarker = (int)(pFirstJPGMarker - (uint8*)pJPEGStream);
	memcpy(pNewStream, pJPEGStream, nSizeToMarker);

	// Comment segment marker
	pNewStream[nSizeToMarker] = 0xFF;
	pNewStream[nSizeToMarker + 1] = 0xFE;

	// Segment length
	int nCommentSegLen = 2 + utf8Length;
	pNewStream[nSizeToMarker + 2] = nCommentSegLen >> 8;
	pNewStream[nSizeToMarker + 3] = nCommentSegLen & 0xFF;

	// Comment and the rest of the JPEG stream
	memcpy(&(pNewStream[nSizeToMarker + 4]), pBuffer, utf8Length);
	memcpy(&(pNewStream[nSizeToMarker + 2 + nCommentSegLen]), &(((uint8*)pJPEGStream)[nSizeToMarker]), nStreamLength - nSizeToMarker);

	nStreamLength += nCommentSegLen + 2;

	delete[] pBuffer;

	return pNewStream;
}

// Gets the thumbnail as a 24 bpp BGR DIB, returns size of thumbnail in sizeThumb
static void* GetThumbnailDIB(CJPEGImage * pImage, CSize& sizeThumb) {
	EProcessingFlags eFlags = pImage->GetLastProcessFlags();
	eFlags = (EProcessingFlags)(eFlags | PFLAG_HighQualityResampling);
	eFlags = (EProcessingFlags)(eFlags & ~PFLAG_AutoContrastSection);
	eFlags = (EProcessingFlags)(eFlags & ~PFLAG_KeepParams);
	CImageProcessingParams params = pImage->GetLastProcessParams();
	params.Sharpen = 0.0;
	double dZoom;
	sizeThumb = Helpers::GetImageRect(pImage->OrigWidth(), pImage->OrigHeight(), 160, 160, Helpers::ZM_FitToScreenNoZoom, dZoom);
	void* pDIB32bpp = pImage->GetThumbnailDIB(sizeThumb, params, eFlags);
	if (pDIB32bpp == NULL) {
		return NULL;
	}
	uint32 nSizeLinePadded = Helpers::DoPadding(sizeThumb.cx*3, 4);
	uint32 nSizeBytes = nSizeLinePadded*sizeThumb.cy;
	char* pDIB24bpp = new char[nSizeBytes];
	CBasicProcessing::Convert32bppTo24bppDIB(sizeThumb.cx, sizeThumb.cy, pDIB24bpp, pDIB32bpp, false);
	return pDIB24bpp;
}

static int GetJFIFBlockLength(unsigned char* pJPEGStream) {
	int nJFIFLength = 0;
	if (pJPEGStream[2] == 0xFF && pJPEGStream[3] == 0xE0) {
		nJFIFLength = pJPEGStream[4]*256 + pJPEGStream[5] + 2;
	}
	return nJFIFLength;
}

// Returns the compressed JPEG stream that must be freed by the caller. NULL in case of error.
static void* CompressAndSave(LPCTSTR sFileName, CJPEGImage * pImage, 
							 void* pData, int nWidth, int nHeight, int nQuality, int& nJPEGStreamLen, 
							 bool& tjFreeNeeded, bool bCopyEXIF, bool bDeleteThumbnail) {
	nJPEGStreamLen = 0;
	tjFreeNeeded = true;
	bool bOutOfMemory;
	unsigned char* pTargetStream = (unsigned char*) TurboJpeg::Compress(pData, nWidth, nHeight, 
		nJPEGStreamLen, bOutOfMemory, nQuality);
	if (pTargetStream == NULL) {
		return NULL;
	}

	FILE *fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		TurboJpeg::Free(pTargetStream);
		return NULL;
	}

	// If EXIF data is present, replace any JFIF block by this EXIF block to preserve the EXIF information
	if (pImage->GetEXIFData() != NULL && bCopyEXIF) {
		const int cnAdditionalThumbBytes = 32000;
		int nEXIFBlockLenCorrection = 0;
		unsigned char* pNewStream = new unsigned char[nJPEGStreamLen + pImage->GetEXIFDataLength() + cnAdditionalThumbBytes];
		memcpy(pNewStream, pTargetStream, 2); // copy SOI block
		memcpy(pNewStream + 2, pImage->GetEXIFData(), pImage->GetEXIFDataLength()); // copy EXIF block
		
		// Set image orientation back to normal orientation, we save the pixels as displayed
		CEXIFReader exifReader(pNewStream + 2, IF_JPEG);
		exifReader.WriteImageOrientation(1); // 1 means default orientation (unrotated)
		if (bDeleteThumbnail) {
			exifReader.DeleteThumbnail();
		} else if (exifReader.HasJPEGCompressedThumbnail()) {
			// recreate EXIF thumbnail image
			CSize sizeThumb;
			void* pDIBThumb = GetThumbnailDIB(pImage, sizeThumb);
			if (pDIBThumb != NULL) {
				int nJPEGThumbStreamLen;
				unsigned char* pJPEGThumb = (unsigned char*) TurboJpeg::Compress(pDIBThumb, sizeThumb.cx, sizeThumb.cy, nJPEGThumbStreamLen, bOutOfMemory, 70);
				if (pJPEGThumb != NULL) {
					int nThumbJFIFLen = GetJFIFBlockLength(pJPEGThumb);
					nEXIFBlockLenCorrection = nJPEGThumbStreamLen - nThumbJFIFLen - exifReader.GetJPEGThumbStreamLen();
					if (nEXIFBlockLenCorrection <= cnAdditionalThumbBytes && pImage->GetEXIFDataLength() + nEXIFBlockLenCorrection < 65536) {
						exifReader.UpdateJPEGThumbnail(pJPEGThumb + 2 + nThumbJFIFLen, nJPEGThumbStreamLen - 2 - nThumbJFIFLen, nEXIFBlockLenCorrection, sizeThumb);
					} else {
						nEXIFBlockLenCorrection = 0;
					}
					delete[] pJPEGThumb;
				}
				delete[] pDIBThumb;
			}
		}

		int nJFIFLength = GetJFIFBlockLength(pTargetStream);
		memcpy(pNewStream + 2 + pImage->GetEXIFDataLength() + nEXIFBlockLenCorrection, pTargetStream + 2 + nJFIFLength, nJPEGStreamLen - 2 - nJFIFLength);
		TurboJpeg::Free(pTargetStream);
		pTargetStream = pNewStream;
		tjFreeNeeded = false;
		nJPEGStreamLen = nJPEGStreamLen - nJFIFLength + pImage->GetEXIFDataLength() + nEXIFBlockLenCorrection;
	}

	// Take over existing JPEG comment from the old image
	LPCTSTR sComment = pImage->GetJPEGComment();
	if (sComment != NULL && sComment[0] != 0) {
		uint8* pNewStream = RemoveExistingCommentSegment(pTargetStream, nJPEGStreamLen);
		if (pNewStream != NULL) {
			if (tjFreeNeeded) TurboJpeg::Free(pTargetStream); else delete[] pTargetStream;
			pTargetStream = pNewStream;
			tjFreeNeeded = false;
		}
		pNewStream = InsertCommentBlock(pTargetStream, nJPEGStreamLen, sComment);
		if (pNewStream != NULL) {
			if (tjFreeNeeded) TurboJpeg::Free(pTargetStream); else delete[] pTargetStream;
			pTargetStream = pNewStream;
			tjFreeNeeded = false;
		}
	}

	bool bSuccess = fwrite(pTargetStream, 1, nJPEGStreamLen, fptr) == nJPEGStreamLen;
	fclose(fptr);

	// delete partial file if no success
	if (!bSuccess) {
		if (tjFreeNeeded) TurboJpeg::Free(pTargetStream); else delete[] pTargetStream;
		_tunlink(sFileName);
		return NULL;
	}

	// Success, return compressed JPEG stream
	return pTargetStream;
}

// pData must point to 24 bit BGR DIB
static bool SaveWebP(LPCTSTR sFileName, void* pData, int nWidth, int nHeight, bool bUseLosslessWEBP) {
	FILE *fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		return false;
	}

	bool bSuccess = false;
	try {
		uint8* pOutput;
		size_t nSize;
		int nQuality = CSettingsProvider::This().WEBPSaveQuality();
		pOutput = (uint8*)WebpReaderWriter::Compress((uint8*)pData, nWidth, nHeight, nSize, nQuality, bUseLosslessWEBP);
		bSuccess = fwrite(pOutput, 1, nSize, fptr) == nSize;
		fclose(fptr);
		WebpReaderWriter::FreeMemory(pOutput);
	} catch(...) {
		fclose(fptr);
	}

	// delete partial file if no success
	if (!bSuccess) {
		_tunlink(sFileName);
		return false;
	}

	return true;
}

// pData must point to 24 bit BGR DIB
static bool SaveQOI(LPCTSTR sFileName, void* pData, int nWidth, int nHeight) {
	FILE* fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		return false;
	}

	bool bSuccess = false;
	try {
		uint8* pOutput;
		int nSize;
		pOutput = (uint8*)QoiReaderWriter::Compress((uint8*)pData, nWidth, nHeight, nSize);
		bSuccess = fwrite(pOutput, 1, nSize, fptr) == nSize;
		fclose(fptr);
		QoiReaderWriter::FreeMemory(pOutput);
	}
	catch (...) {
		fclose(fptr);
	}

	// delete partial file if no success
	if (!bSuccess) {
		_tunlink(sFileName);
		return false;
	}

	return true;
}

CAvifEncoder::CAvifEncoder():
	m_sFileName(0),
	m_fOutput(0),
	m_pEncoder(0),
	m_pAvifOuput(0),
	m_nWidth(0), m_nHeight(0),
	m_nTimeScaleHz(1), m_nFrameIntervalMs(1000),
	m_nQuality(60),
	m_bLossless(false),
	m_bSuccess(false)
{
}

CAvifEncoder::~CAvifEncoder()
{
	if (m_pEncoder)
	{
		avifEncoderDestroy(m_pEncoder);
		m_pEncoder = 0;
	}
}

/*
* Initialize AVIF image encoder by creating file and setting various image properties
*/
bool CAvifEncoder::Init(LPCTSTR sFileName, int nWidth, int nHeight, bool bUseLossless, int nQuality, uint64_t nFrameIntervalMs)
{
	if (m_pEncoder || m_pAvifOuput)
	{
		return false; //cannot init twice! abort
	}
	m_sFileName = sFileName;
	m_fOutput = _tfopen(sFileName, _T("wb"));
	if (!m_fOutput) {
		return false;
	}

	memset(&m_rgb, 0, sizeof(m_rgb));
	m_nWidth = nWidth;
	m_nHeight = nHeight;

	m_pEncoder = avifEncoderCreate();
	// Configure your encoder here (see avif/avif.h):
	// * maxThreads
	// * quality
	// * qualityAlpha
	// * tileRowsLog2
	// * tileColsLog2
	// * speed
	// * keyframeInterval
	// * timescale
	m_pEncoder->quality = m_nQuality = nQuality;
	m_bLossless = bUseLossless;
	m_pEncoder->qualityAlpha = bUseLossless ? AVIF_QUALITY_LOSSLESS : AVIF_QUALITY_DEFAULT;
	m_nFrameIntervalMs = nFrameIntervalMs;
	m_nTimeScaleHz = (uint64_t)(1.0 / (0.001 * nFrameIntervalMs));
	m_pEncoder->timescale = m_nTimeScaleHz;

	return true;
}

/*
bool CAvifEncoder::WriteSingleImage(void* pData, bool bUseLossless, int nQuality)
{
	return AppendImage(pData, bUseLossless, nQuality);
}
*/

/*
* Write single image of non-animated AVIF
*/
bool CAvifEncoder::WriteSingleImage(void* pData, bool bUseLossless, int nQuality)
{
	return WriteImage(pData, bUseLossless, nQuality);
}

/*
* Append an image frame for an animated AVIF
*/
bool CAvifEncoder::AppendImage(void* pData, bool bUseLossless, int nQuality,
	uint64_t nFrameIntervalMs, bool bKeyFrame)
{
	return WriteImage(pData, bUseLossless, nQuality, nFrameIntervalMs,
		bKeyFrame? AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME: AVIF_ADD_IMAGE_FLAG_NONE);
}

/*
* [Internal use] Write an image frame of AVIF
* nFrameIntervalMs: duration of this frame in ms; applies to animated AVIF only
* nFrameType: AVIF_ADD_IMAGE_FLAG_SINGLE = single image of a non-animated AVIF
*			  AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME = keyframe of an animated AVIF
*			  AVIF_ADD_IMAGE_FLAG_NONE = subsequent frame of an animated AVIF
*/
bool CAvifEncoder::WriteImage(void* pData, bool bUseLossless, int nQuality,
	uint64_t nFrameIntervalMs, avifAddImageFlag nFrameType)
{
	if (!m_pEncoder || !m_fOutput
		|| m_pAvifOuput) //already finished output, so no more writes allowed
	{
		return false;
	}
	bool bOk = false;
	avifImage* image = 0;
	try {
		image = avifImageCreate(m_nWidth, m_nHeight, 8, AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF

		avifRGBImageSetDefaults(&m_rgb, image);
		// Override RGB(A)->YUV(A) defaults here:
		//   depth, format, chromaDownsampling, avoidLibYUV, ignoreAlpha, alphaPremultiplied, etc.
		m_rgb.format = AVIF_RGB_FORMAT_BGRA; //CJPEGImage provides BGRA
		m_rgb.rowBytes = 4 * m_nWidth;
		m_rgb.pixels = (uint8_t*)pData;

		avifResult result = avifImageRGBToYUV(image, &m_rgb);
		if (result == AVIF_RESULT_OK)
		{
			uint64_t nDurationInNumOfTimescales = 1;
			if (nFrameType != AVIF_ADD_IMAGE_FLAG_SINGLE)
			{
				if (nFrameIntervalMs != 0)
				{
					nDurationInNumOfTimescales = (uint64_t)(((double)nFrameIntervalMs) / m_nFrameIntervalMs);
					if (nDurationInNumOfTimescales < 1)
						nDurationInNumOfTimescales = 1;
				}
			}
			// Add single image in your sequence
			result = avifEncoderAddImage(m_pEncoder, image, nDurationInNumOfTimescales, nFrameType);
			if (result == AVIF_RESULT_OK)
			{
				bOk = true;
			}
			//else fprintf(stderr, "Failed to add image to encoder: %s\n", avifResultToString(addImageResult));
		}
		/*
		else
		{
			TCHAR buffer[100];
			CString s(avifResultToString(convertResult));
			_stprintf_s(buffer, 100, _T("Failed to convert to YUV(A): %s"), s.GetString());
			::MessageBox(NULL, CString(_T("Save As AVIF: ")) + buffer, _T("Error"), MB_OK);
		}
		*/
	}
	catch (...) {
	}
	if (image) {
		avifImageDestroy(image);
	}
	return bOk;
}

/*
* Cleanup. Delete file if not complete success.
* Retval: true if encoding succeeded; false o.w.
*/
bool CAvifEncoder::Finish()
{
	if (!m_pEncoder)
	{
		return false;
	}
	try {
		m_pAvifOuput = new avifRWData;
		*m_pAvifOuput = AVIF_DATA_EMPTY;

		avifResult result = avifEncoderFinish(m_pEncoder, m_pAvifOuput);
		if (result == AVIF_RESULT_OK)
		{
			size_t bytesWritten = fwrite(m_pAvifOuput->data, 1, m_pAvifOuput->size, m_fOutput);
			if (bytesWritten == m_pAvifOuput->size)
			{
				m_bSuccess = true;
			}
			//else fprintf(stderr, "Failed to write %zu bytes\n", avifOutput.size);
		}
		//else fprintf(stderr, "Failed to finish encode: %s\n", avifResultToString(finishResult));
	}
	catch (...) {
	}
	if (m_fOutput)
	{
		fclose(m_fOutput);
		m_fOutput = 0;
	}
	if (m_pAvifOuput)
	{
		avifRWDataFree(m_pAvifOuput);
		delete m_pAvifOuput;
		m_pAvifOuput = 0;
	}
	if (!m_bSuccess) {
		_tunlink(m_sFileName);
	}
	return m_bSuccess;
}

// Convenience method to write a non-animated AVIF image
bool CAvifEncoder::SaveAVIF(LPCTSTR sFileName, void* pData, int nWidth, int nHeight, bool bUseLossless, int nQuality)
{
	if (Init(sFileName, nWidth, nHeight, bUseLossless, nQuality))
	{
		if (WriteSingleImage(pData, bUseLossless, nQuality))
		{
			return Finish();
		}
	}
	return false;
}

// Convenience method to write a non-animated AVIF image
static bool SaveAVIF(LPCTSTR sFileName, void* pData, int nWidth, int nHeight, bool bUseLossless)
{
	CAvifEncoder enc;
	return enc.SaveAVIF(sFileName, pData, nWidth, nHeight, bUseLossless);
}

// Copied from MS sample
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
   UINT  num = 0;          // number of image encoders
   UINT  size = 0;         // size of the image encoder array in bytes

   Gdiplus::GetImageEncodersSize(&num, &size);
   if(size == 0)
	  return -1;  // Failure

   Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));

   Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

   for(UINT j = 0; j < num; ++j) {
	  if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 ) {
		 *pClsid = pImageCodecInfo[j].Clsid;
		 free(pImageCodecInfo);
		 return j;  // Success
	  }    
   }

   free(pImageCodecInfo);
   return -1;  // Failure
}

// Saves the given 24 bpp DIB data to the file name given using GDI+
static bool SaveGDIPlus(LPCTSTR sFileName, EImageFormat eFileFormat, void* pData, int nWidth, int nHeight) {
	Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(nWidth, nHeight, Helpers::DoPadding(nWidth*3, 4), PixelFormat24bppRGB, (BYTE*)pData);
	if (pBitmap->GetLastStatus() != Gdiplus::Ok) {
		delete pBitmap;
		return false;
	}

	const wchar_t* sMIMEType = NULL;
	switch (eFileFormat) {
		case IF_WindowsBMP:
			sMIMEType = L"image/bmp";
			break;
		case IF_PNG:
			sMIMEType = L"image/png";
			break;
		case IF_TIFF:
			sMIMEType = L"image/tiff";
			break;
	}

	CLSID encoderClsid;
	int result = (sMIMEType == NULL) ? -1 : GetEncoderClsid(sMIMEType, &encoderClsid);
	if (result < 0) {
		delete pBitmap;
		return false;
	}

	const wchar_t* sFileNameUnicode;
	sFileNameUnicode = (const wchar_t*)sFileName;

	bool bOk = pBitmap->Save(sFileNameUnicode, &encoderClsid, NULL) == Gdiplus::Ok;

	delete pBitmap;
	return bOk;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CSaveImage
//////////////////////////////////////////////////////////////////////////////////////////////

bool CSaveImage::SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
			 EProcessingFlags eFlags, bool bFullSize, bool bUseLosslessWEBP, bool bCreateParameterDBEntry) {
	pImage->EnableDimming(false);

	CSize imageSize;
	void* pDIB32bpp;

	if (bFullSize) {
		imageSize = pImage->OrigSize();
		pDIB32bpp = pImage->GetDIB(imageSize, imageSize, CPoint(0, 0), procParams, eFlags);
	} else {
		imageSize = CSize(pImage->DIBWidth(), pImage->DIBHeight());
		pDIB32bpp = pImage->DIBPixelsLastProcessed(true);
	}

	if (pDIB32bpp == NULL) {
		pImage->EnableDimming(true);
		return false;
	}

	uint32 nSizeLinePadded = Helpers::DoPadding(imageSize.cx*3, 4);
	uint32 nSizeBytes = nSizeLinePadded*imageSize.cy;
	char* pDIB24bpp = 0;
	EImageFormat eFileFormat = Helpers::GetImageFormat(sFileName);
	if (eFileFormat != IF_AVIF)
	{
		pDIB24bpp = new char[nSizeBytes];
		CBasicProcessing::Convert32bppTo24bppDIB(imageSize.cx, imageSize.cy, pDIB24bpp, pDIB32bpp, false);
	}

	bool bSuccess = false;
	__int64 nPixelHash = 0;
	if (eFileFormat == IF_JPEG || eFileFormat == IF_JPEG_Embedded) {
		// Save JPEG not over GDI+ - we want to keep the meta-data if there is meta-data
		int nJPEGStreamLen;
		bool tjFreeNeeded;
		void* pCompressedJPEG = CompressAndSave(sFileName, pImage, pDIB24bpp, imageSize.cx, imageSize.cy, 
			CSettingsProvider::This().JPEGSaveQuality(), nJPEGStreamLen, tjFreeNeeded, true, !bFullSize);
		bSuccess = pCompressedJPEG != NULL;
		if (bSuccess) {
			nPixelHash = Helpers::CalculateJPEGFileHash(pCompressedJPEG, nJPEGStreamLen);
			if (tjFreeNeeded) {
				TurboJpeg::Free((unsigned char*)pCompressedJPEG);
			} else {
				delete[] pCompressedJPEG;
			}
		}
	} else {
		if (eFileFormat == IF_WEBP) {
			bSuccess = SaveWebP(sFileName, pDIB24bpp, imageSize.cx, imageSize.cy, bUseLosslessWEBP);
		} else if (eFileFormat == IF_AVIF) {
			bSuccess = SaveAVIF(sFileName, pDIB32bpp, imageSize.cx, imageSize.cy, bUseLosslessWEBP);
		} else if (eFileFormat == IF_QOI) {
			bSuccess = SaveQOI(sFileName, pDIB24bpp, imageSize.cx, imageSize.cy);
		} else {
			bSuccess = SaveGDIPlus(sFileName, eFileFormat, pDIB24bpp, imageSize.cx, imageSize.cy);
		}
		if (bSuccess) {
			CJPEGImage tempImage(imageSize.cx, imageSize.cy, pDIB32bpp, NULL, 4, 0, IF_Unknown, false, 0, 1, 0);
			nPixelHash = tempImage.GetUncompressedPixelHash();
			tempImage.DetachOriginalPixels();
		}
	}

	if (pDIB24bpp)
	{
		delete[] pDIB24bpp;
		pDIB24bpp = NULL;
	}

	// Create database entry to avoid processing image again
	if (bSuccess && bCreateParameterDBEntry && CSettingsProvider::This().CreateParamDBEntryOnSave()) {
		if (nPixelHash != 0) {
			CParameterDBEntry newEntry;
			CImageProcessingParams ippNone(0.0, 1.0, 1.0, CSettingsProvider::This().Sharpen(), 0.0, 0.5, 0.5, 0.25, 0.5, 0.0, 0.0, 0.0);
			EProcessingFlags procFlagsNone = GetProcessingFlag(eFlags, PFLAG_HighQualityResampling) ? PFLAG_HighQualityResampling : PFLAG_None;
			newEntry.InitFromProcessParams(ippNone, procFlagsNone, CRotationParams(0));
			newEntry.SetHash(nPixelHash);
			CParameterDB::This().AddEntry(newEntry);
		}
	}
	pImage->EnableDimming(true);

	return bSuccess;
}

bool CSaveImage::SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, bool bUseLosslessWEBP)
{
	CImageProcessingParams paramsNotUsed;
	return SaveImage(sFileName, pImage, paramsNotUsed, PFLAG_None, false, bUseLosslessWEBP, false);
}
