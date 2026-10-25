// D3DCompile for DXVK-native builds. vkd3d-utils provides the HLSL compiler, but it is built
// with the Windows x64 calling convention (its WINAPI and COM methods are ms_abi) while
// DXVK-native's headers, which the rest of the runtime uses, declare everything with the
// platform convention. CMakeLists.txt renames D3DCompile / D3DCreateBlob to these
// Host_ functions; they call vkd3d with the right convention and hand back blobs with
// DXVK's ABI.
#include <d3dcompiler.h>

#include <atomic>
#include <cstring>
#include <vector>

#undef D3DCompile
#undef D3DCreateBlob

#define VKD3D_ABI __attribute__((ms_abi))

namespace
{
// vkd3d's ID3DBlob (an ms_abi COM object)
struct VkdBlob;
struct VkdBlobVtbl
{
    HRESULT(VKD3D_ABI* QueryInterface)(VkdBlob*, const GUID&, void**);
    ULONG(VKD3D_ABI* AddRef)(VkdBlob*);
    ULONG(VKD3D_ABI* Release)(VkdBlob*);
    void*(VKD3D_ABI* GetBufferPointer)(VkdBlob*);
    SIZE_T(VKD3D_ABI* GetBufferSize)(VkdBlob*);
};
struct VkdBlob
{
    const VkdBlobVtbl* vtbl;
};

class HostBlob final : public ID3DBlob
{
public:
    explicit HostBlob(size_t size) : m_data(size) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D10Blob))
        {
            AddRef();
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = --m_refs;
        if (!n) delete this;
        return n;
    }
    LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return m_data.data(); }
    SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return m_data.size(); }

private:
    std::vector<uint8_t> m_data;
    std::atomic<ULONG> m_refs = 1;
};

// copies a vkd3d blob into a HostBlob and releases it
ID3DBlob* Adopt(VkdBlob* b)
{
    if (!b) return nullptr;
    size_t n = b->vtbl->GetBufferSize(b);
    auto* out = new HostBlob(n);
    if (n) memcpy(out->GetBufferPointer(), b->vtbl->GetBufferPointer(b), n);
    b->vtbl->Release(b);
    return out;
}
}

extern "C" VKD3D_ABI HRESULT D3DCompile(const void* data, SIZE_T dataSize, const char* filename,
                                        const D3D_SHADER_MACRO* defines, void* include, const char* entrypoint,
                                        const char* target, UINT flags, UINT effectFlags, VkdBlob** shader,
                                        VkdBlob** errors);

extern "C" HRESULT Host_D3DCompile(const void* data, SIZE_T dataSize, const char* filename,
                                   const D3D_SHADER_MACRO* defines, ID3DInclude* include, const char* entrypoint,
                                   const char* target, UINT flags, UINT effectFlags, ID3DBlob** shader,
                                   ID3DBlob** errors)
{
    if (include) return E_NOTIMPL;   // ID3DInclude callbacks would need the same ABI bridging
    VkdBlob *s = nullptr, *e = nullptr;
    HRESULT hr = D3DCompile(data, dataSize, filename, defines, nullptr, entrypoint, target, flags, effectFlags,
                            &s, errors ? &e : nullptr);
    if (shader) *shader = Adopt(s);
    else if (s) s->vtbl->Release(s);
    if (errors) *errors = Adopt(e);
    return hr;
}

extern "C" HRESULT Host_D3DCreateBlob(SIZE_T size, ID3DBlob** blob)
{
    *blob = new HostBlob(size);
    return S_OK;
}
