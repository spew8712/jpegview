#pragma once

#include "ProcessParams.h"
#include "avif/avif.h"

class CJPEGImage;

// Class that enables to save a processed image to a one of the supported file formats
class CSaveImage
{
public:
	// Save processed image to file. Returns if saving was successful or not.
	// The file format is derived from the file ending of the specified file name.
	// If bFullSize is true, the image in its original size is processed and saved.
	// If bFullSize is false, the image section as shown in the window is saved.
	// Processing parameters and flags are ignored when bFullSize is false, the image is not reprocessed in this case.
	static bool SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, bool bFullSize, bool bUseLosslessWEBP, bool bCreateParameterDBEntry = true);

	// Saves processed image in current window size as displayed on screen. Creates no parameter DB entry for the saved image.
	// The file format is derived from the file ending of the specified file name.
	static bool SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, bool bUseLosslessWEBP);

private:
	CSaveImage(void);
};

class CAvifEncoder
{
public:
	CAvifEncoder();
	~CAvifEncoder();

	bool Init(LPCTSTR sFileName, int nWidth, int nHeight, bool bUseLossless = false, int nQuality = 60, uint64_t nFrameIntervalMs = 1000);
	bool WriteSingleImage(void* pData, bool bUseLossless = false, int nQuality = 60);
	bool AppendImage(void* pData, bool bUseLossless = false, int nQuality = 60,
		uint64_t nFrameIntervalMs = 0, bool bKeyFrame = false);
	bool Finish();
	bool SaveAVIF(LPCTSTR sFileName, void* pData, int nWidth, int nHeight, bool bUseLossless = false, int nQuality = 60);
private:
	bool WriteImage(void* pData, bool bUseLossless = false, int nQuality = 60,
		uint64_t nFrameIntervalMs = 0, avifAddImageFlag nFrameType = AVIF_ADD_IMAGE_FLAG_SINGLE);
	LPCTSTR m_sFileName;
	FILE* m_fOutput;
	avifEncoder *m_pEncoder;
	avifRWData *m_pAvifOuput;
	avifRGBImage m_rgb;
	int m_nWidth, m_nHeight;
	uint64_t m_nTimeScaleHz, m_nFrameIntervalMs;
	int m_nQuality;
	bool m_bLossless, m_bSuccess;
};