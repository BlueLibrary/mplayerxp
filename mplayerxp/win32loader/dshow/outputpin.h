#ifndef DS_OUTPUTPIN_H
#define DS_OUTPUTPIN_H

/* "output pin" - the one that connects to output of filter. */

#include "allocator.h"

typedef struct _COutputMemPin COutputMemPin;
typedef struct _COutputPin COutputPin;

/**
 Callback routine for copying samples from pin into filter
 \param pUserData pointer to user's data
 \param sample IMediaSample
*/
typedef  HRESULT STDCALL (*SAMPLEPROC)(any_t* pUserData,IMediaSample*sample);

struct _COutputPin
{
    IPin_vt* vt;
    DECLARE_IUNKNOWN();
    COutputMemPin* mempin;
    AM_MEDIA_TYPE type;
    IPin* remote;
    SAMPLEPROC SampleProc;
    any_t* pUserData;
    void ( *SetNewFormat )(COutputPin*, const AM_MEDIA_TYPE* a);
};

COutputPin* COutputPinCreate(const AM_MEDIA_TYPE* amt,SAMPLEPROC SampleProc,any_t* pUserData);

#endif /* DS_OUTPUTPIN_H */