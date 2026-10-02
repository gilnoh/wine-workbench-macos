/* Open a file the way Unity's WindowsVideoMedia does (IMFSourceReader from URL),
 * then try to decode video from it, reporting each step's HRESULT.
 *
 *   mfprobe.exe <path>
 *
 * c00d36c4 at CreateObjectFromURL = no byte stream handler for the container.
 * c00d5212 at SetCurrentMediaType = container opened, but no decoder MFT. */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

int wmain(int argc, WCHAR **argv)
{
    IMFSourceResolver *resolver = NULL;
    IMFSourceReader *reader = NULL;
    MF_OBJECT_TYPE type;
    IUnknown *obj = NULL;
    HRESULT hr;

    if (argc < 2) { printf("usage: mfprobe <file>\n"); return 2; }
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("CoInitializeEx            hr=%08lx\n", hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    printf("MFStartup                 hr=%08lx\n", hr);

    hr = MFCreateSourceResolver(&resolver);
    printf("MFCreateSourceResolver    hr=%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IMFSourceResolver_CreateObjectFromURL(resolver, argv[1], MF_RESOLUTION_MEDIASOURCE, NULL, &type, &obj);
        printf("CreateObjectFromURL       hr=%08lx\n", hr);
        if (obj) IUnknown_Release(obj);
    }

    hr = MFCreateSourceReaderFromURL(argv[1], NULL, &reader);
    printf("MFCreateSourceReaderFromURL hr=%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        IMFMediaType *mt;
        DWORD i;
        for (i = 0; SUCCEEDED(IMFSourceReader_GetNativeMediaType(reader, i, 0, &mt)); i++)
        {
            GUID major, sub;
            IMFMediaType_GetGUID(mt, &MF_MT_MAJOR_TYPE, &major);
            IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &sub);
            printf("  stream %lu major=%08lx subtype=%08lx\n", i, major.Data1, sub.Data1);
            IMFMediaType_Release(mt);
        }
        {
            IMFMediaType *out; IMFSample *sample; DWORD idx, fl; LONGLONG ts; int n, got = 0;
            const GUID *subs[] = { &MFVideoFormat_NV12, &MFVideoFormat_RGB32 };
            for (n = 0; n < 2; n++) {
                MFCreateMediaType(&out);
                IMFMediaType_SetGUID(out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
                IMFMediaType_SetGUID(out, &MF_MT_SUBTYPE, subs[n]);
                hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, out);
                printf("SetCurrentMediaType(%s) hr=%08lx\n", n ? "RGB32" : "NV12", hr);
                IMFMediaType_Release(out);
                if (SUCCEEDED(hr)) break;
            }
            for (n = 0; n < 30 && SUCCEEDED(hr); n++) {
                sample = NULL;
                hr = IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &fl, &ts, &sample);
                if (sample) { got++; IMFSample_Release(sample); }
                if (fl & MF_SOURCE_READERF_ENDOFSTREAM) break;
            }
            printf("ReadSample x%d hr=%08lx decoded_frames=%d last_ts=%lld\n", n, hr, got, ts);
        }
        IMFSourceReader_Release(reader);
    }
    MFShutdown();
    return SUCCEEDED(hr) ? 0 : 1;
}
