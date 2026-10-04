/* Helpers shared by the Bluetooth runtime classes
 *
 * Copyright 2026 Jelmer Vernooĳ
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "private.h"
#include "setupapi.h"
#include "roapi.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetooth );

HRESULT buffer_create( const BYTE *data, UINT32 size, IBuffer **out )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Storage_Streams_Buffer;
    IBufferByteAccess *access;
    IBufferFactory *factory;
    HSTRING_HEADER hdr;
    HSTRING str;
    HRESULT hr;
    BYTE *dst;

    *out = NULL;
    if (FAILED(hr = WindowsCreateStringReference( class_name, wcslen( class_name ), &hdr, &str ))) return hr;
    if (FAILED(hr = RoGetActivationFactory( str, &IID_IBufferFactory, (void **)&factory ))) return hr;
    hr = IBufferFactory_Create( factory, size, out );
    IBufferFactory_Release( factory );
    if (FAILED(hr)) return hr;

    if (SUCCEEDED(hr = IBuffer_QueryInterface( *out, &IID_IBufferByteAccess, (void **)&access )))
    {
        if (SUCCEEDED(hr = IBufferByteAccess_Buffer( access, &dst )))
        {
            memcpy( dst, data, size );
            hr = IBuffer_put_Length( *out, size );
        }
        IBufferByteAccess_Release( access );
    }
    if (FAILED(hr))
    {
        IBuffer_Release( *out );
        *out = NULL;
    }
    return hr;
}

/* The returned data is only valid for as long as the buffer is. */
HRESULT buffer_get_bytes( IBuffer *buffer, BYTE **data, UINT32 *size )
{
    IBufferByteAccess *access;
    HRESULT hr;

    *data = NULL;
    *size = 0;
    if (!buffer) return S_OK;
    if (FAILED(hr = IBuffer_get_Length( buffer, size ))) return hr;
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access ))) return hr;
    hr = IBufferByteAccess_Buffer( access, data );
    IBufferByteAccess_Release( access );
    return hr;
}

/* IReference<T> for a T of at most 8 bytes. */
struct reference
{
    IInspectable IInspectable_iface;
    LONG ref;
    GUID iid;
    UINT32 size;
    BYTE value[8];
};

static inline struct reference *impl_from_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct reference, IInspectable_iface );
}

static HRESULT WINAPI reference_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct reference *impl = impl_from_IInspectable( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ))
    {
        IInspectable_AddRef(( *out = iface ));
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI reference_AddRef( IInspectable *iface )
{
    struct reference *impl = impl_from_IInspectable( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI reference_Release( IInspectable *iface )
{
    struct reference *impl = impl_from_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI reference_GetIids( IInspectable *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI reference_GetRuntimeClassName( IInspectable *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI reference_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI reference_get_Value( IInspectable *iface, void *value )
{
    struct reference *impl = impl_from_IInspectable( iface );

    if (!value) return E_POINTER;
    memcpy( value, impl->value, impl->size );
    return S_OK;
}

static const struct
{
    IInspectableVtbl inspectable;
    HRESULT (WINAPI *get_Value)( IInspectable *iface, void *value );
} reference_vtbl =
{
    {
        reference_QueryInterface,
        reference_AddRef,
        reference_Release,
        reference_GetIids,
        reference_GetRuntimeClassName,
        reference_GetTrustLevel,
    },
    reference_get_Value,
};

HRESULT reference_create( const GUID *iid, const void *value, UINT32 size, IInspectable **out )
{
    struct reference *impl;

    if (size > sizeof( impl->value )) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = &reference_vtbl.inspectable;
    impl->ref = 1;
    impl->iid = *iid;
    impl->size = size;
    memcpy( impl->value, value, size );
    *out = &impl->IInspectable_iface;
    return S_OK;
}

/* Open the first Bluetooth radio, returning INVALID_HANDLE_VALUE if there is none. */
HANDLE open_bluetooth_radio( DWORD flags )
{
    char buffer[sizeof( SP_DEVICE_INTERFACE_DETAIL_DATA_W ) + MAX_PATH * sizeof( WCHAR )];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *iface_detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)buffer;
    SP_DEVICE_INTERFACE_DATA iface_data = { .cbSize = sizeof( iface_data ) };
    HANDLE radio = INVALID_HANDLE_VALUE;
    HDEVINFO devinfo;
    DWORD idx = 0;

    devinfo = SetupDiGetClassDevsW( &GUID_BTHPORT_DEVICE_INTERFACE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE );
    if (devinfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    iface_detail->cbSize = sizeof( *iface_detail );
    while (radio == INVALID_HANDLE_VALUE &&
           SetupDiEnumDeviceInterfaces( devinfo, NULL, &GUID_BTHPORT_DEVICE_INTERFACE, idx++, &iface_data ))
    {
        if (!SetupDiGetDeviceInterfaceDetailW( devinfo, &iface_data, iface_detail, sizeof( buffer ), NULL, NULL ))
            continue;
        radio = CreateFileW( iface_detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, flags, NULL );
    }
    SetupDiDestroyDeviceInfoList( devinfo );
    return radio;
}
