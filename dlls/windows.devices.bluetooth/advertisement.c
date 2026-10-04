/* windows.Devices.Bluetooth.Advertisement Implementation
 *
 * Copyright 2025 Vibhav Pant
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
#include "roapi.h"
#include "winioctl.h"
#include "wine/winebth.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetooth );

/* Advertising data types, from the Bluetooth assigned numbers. */
#define AD_TYPE_FLAGS                     0x01
#define AD_TYPE_INCOMPLETE_16BIT_UUIDS    0x02
#define AD_TYPE_COMPLETE_16BIT_UUIDS      0x03
#define AD_TYPE_INCOMPLETE_128BIT_UUIDS   0x06
#define AD_TYPE_COMPLETE_128BIT_UUIDS     0x07
#define AD_TYPE_SHORTENED_LOCAL_NAME      0x08
#define AD_TYPE_COMPLETE_LOCAL_NAME       0x09
#define AD_TYPE_TX_POWER_LEVEL            0x0a
#define AD_TYPE_MANUFACTURER_DATA         0xff

struct data_section
{
    IBluetoothLEAdvertisementDataSection IBluetoothLEAdvertisementDataSection_iface;
    LONG ref;

    BYTE type;
    IBuffer *data;
};

static inline struct data_section *impl_from_IBluetoothLEAdvertisementDataSection( IBluetoothLEAdvertisementDataSection *iface )
{
    return CONTAINING_RECORD( iface, struct data_section, IBluetoothLEAdvertisementDataSection_iface );
}

static HRESULT WINAPI data_section_QueryInterface( IBluetoothLEAdvertisementDataSection *iface, REFIID iid, void **out )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementDataSection ))
    {
        IBluetoothLEAdvertisementDataSection_AddRef(( *out = &impl->IBluetoothLEAdvertisementDataSection_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI data_section_AddRef( IBluetoothLEAdvertisementDataSection *iface )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI data_section_Release( IBluetoothLEAdvertisementDataSection *iface )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        if (impl->data) IBuffer_Release( impl->data );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( data_section, IBluetoothLEAdvertisementDataSection, RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementDataSection )

static HRESULT WINAPI data_section_get_DataType( IBluetoothLEAdvertisementDataSection *iface, BYTE *value )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->type;
    return S_OK;
}

static HRESULT WINAPI data_section_put_DataType( IBluetoothLEAdvertisementDataSection *iface, BYTE value )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );

    TRACE( "(%p, %#x)\n", iface, value );

    impl->type = value;
    return S_OK;
}

static HRESULT WINAPI data_section_get_Data( IBluetoothLEAdvertisementDataSection *iface, IBuffer **value )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->data)) IBuffer_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI data_section_put_Data( IBluetoothLEAdvertisementDataSection *iface, IBuffer *value )
{
    struct data_section *impl = impl_from_IBluetoothLEAdvertisementDataSection( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (value) IBuffer_AddRef( value );
    if (impl->data) IBuffer_Release( impl->data );
    impl->data = value;
    return S_OK;
}

static const IBluetoothLEAdvertisementDataSectionVtbl data_section_vtbl =
{
    /* IUnknown */
    data_section_QueryInterface,
    data_section_AddRef,
    data_section_Release,
    /* IInspectable */
    data_section_GetIids,
    data_section_GetRuntimeClassName,
    data_section_GetTrustLevel,
    /* IBluetoothLEAdvertisementDataSection */
    data_section_get_DataType,
    data_section_put_DataType,
    data_section_get_Data,
    data_section_put_Data,
};

static HRESULT data_section_create( BYTE type, const BYTE *data, UINT32 size, IBluetoothLEAdvertisementDataSection **out )
{
    struct data_section *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = buffer_create( data, size, &impl->data )))
    {
        free( impl );
        return hr;
    }
    impl->IBluetoothLEAdvertisementDataSection_iface.lpVtbl = &data_section_vtbl;
    impl->ref = 1;
    impl->type = type;
    *out = &impl->IBluetoothLEAdvertisementDataSection_iface;
    return S_OK;
}

struct manufacturer_data
{
    IBluetoothLEManufacturerData IBluetoothLEManufacturerData_iface;
    LONG ref;

    UINT16 company;
    IBuffer *data;
};

static inline struct manufacturer_data *impl_from_IBluetoothLEManufacturerData( IBluetoothLEManufacturerData *iface )
{
    return CONTAINING_RECORD( iface, struct manufacturer_data, IBluetoothLEManufacturerData_iface );
}

static HRESULT WINAPI manufacturer_data_QueryInterface( IBluetoothLEManufacturerData *iface, REFIID iid, void **out )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEManufacturerData ))
    {
        IBluetoothLEManufacturerData_AddRef(( *out = &impl->IBluetoothLEManufacturerData_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI manufacturer_data_AddRef( IBluetoothLEManufacturerData *iface )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI manufacturer_data_Release( IBluetoothLEManufacturerData *iface )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        if (impl->data) IBuffer_Release( impl->data );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( manufacturer_data, IBluetoothLEManufacturerData, RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEManufacturerData )

static HRESULT WINAPI manufacturer_data_get_CompanyId( IBluetoothLEManufacturerData *iface, UINT16 *value )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->company;
    return S_OK;
}

static HRESULT WINAPI manufacturer_data_put_CompanyId( IBluetoothLEManufacturerData *iface, UINT16 value )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );

    TRACE( "(%p, %#x)\n", iface, value );

    impl->company = value;
    return S_OK;
}

static HRESULT WINAPI manufacturer_data_get_Data( IBluetoothLEManufacturerData *iface, IBuffer **value )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->data)) IBuffer_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI manufacturer_data_put_Data( IBluetoothLEManufacturerData *iface, IBuffer *value )
{
    struct manufacturer_data *impl = impl_from_IBluetoothLEManufacturerData( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (value) IBuffer_AddRef( value );
    if (impl->data) IBuffer_Release( impl->data );
    impl->data = value;
    return S_OK;
}

static const IBluetoothLEManufacturerDataVtbl manufacturer_data_vtbl =
{
    /* IUnknown */
    manufacturer_data_QueryInterface,
    manufacturer_data_AddRef,
    manufacturer_data_Release,
    /* IInspectable */
    manufacturer_data_GetIids,
    manufacturer_data_GetRuntimeClassName,
    manufacturer_data_GetTrustLevel,
    /* IBluetoothLEManufacturerData */
    manufacturer_data_get_CompanyId,
    manufacturer_data_put_CompanyId,
    manufacturer_data_get_Data,
    manufacturer_data_put_Data,
};

static HRESULT manufacturer_data_create( UINT16 company, const BYTE *data, UINT32 size, IBluetoothLEManufacturerData **out )
{
    struct manufacturer_data *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = buffer_create( data, size, &impl->data )))
    {
        free( impl );
        return hr;
    }
    impl->IBluetoothLEManufacturerData_iface.lpVtbl = &manufacturer_data_vtbl;
    impl->ref = 1;
    impl->company = company;
    *out = &impl->IBluetoothLEManufacturerData_iface;
    return S_OK;
}

/* A read-only IVector<GUID>. */
struct uuid_vector
{
    IVector_GUID IVector_GUID_iface;
    IVectorView_GUID IVectorView_GUID_iface;
    LONG ref;

    UINT32 count;
    GUID uuids[1];
};

static inline struct uuid_vector *impl_from_IVector_GUID( IVector_GUID *iface )
{
    return CONTAINING_RECORD( iface, struct uuid_vector, IVector_GUID_iface );
}

static HRESULT WINAPI uuid_vector_QueryInterface( IVector_GUID *iface, REFIID iid, void **out )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVector_GUID ))
    {
        IVector_GUID_AddRef(( *out = &impl->IVector_GUID_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IVectorView_GUID ))
    {
        IVectorView_GUID_AddRef(( *out = &impl->IVectorView_GUID_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI uuid_vector_AddRef( IVector_GUID *iface )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uuid_vector_Release( IVector_GUID *iface )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( uuid_vector, IVector_GUID, L"Windows.Foundation.Collections.IVector`1<Guid>" )

static HRESULT WINAPI uuid_vector_GetAt( IVector_GUID *iface, UINT32 index, GUID *value )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );

    if (index >= impl->count) return E_BOUNDS;
    *value = impl->uuids[index];
    return S_OK;
}

static HRESULT WINAPI uuid_vector_get_Size( IVector_GUID *iface, UINT32 *value )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );

    *value = impl->count;
    return S_OK;
}

static HRESULT WINAPI uuid_vector_GetView( IVector_GUID *iface, IVectorView_GUID **value )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );

    IVectorView_GUID_AddRef(( *value = &impl->IVectorView_GUID_iface ));
    return S_OK;
}

static HRESULT WINAPI uuid_vector_IndexOf( IVector_GUID *iface, GUID element, UINT32 *index, BOOLEAN *found )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );
    UINT32 i;

    for (i = 0; i < impl->count; i++) if (IsEqualGUID( &impl->uuids[i], &element )) break;
    if ((*found = i < impl->count)) *index = i;
    else *index = 0;
    return S_OK;
}

static HRESULT WINAPI uuid_vector_SetAt( IVector_GUID *iface, UINT32 index, GUID value )
{
    FIXME( "(%p, %u, %s): stub!\n", iface, index, debugstr_guid( &value ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI uuid_vector_InsertAt( IVector_GUID *iface, UINT32 index, GUID value )
{
    FIXME( "(%p, %u, %s): stub!\n", iface, index, debugstr_guid( &value ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI uuid_vector_RemoveAt( IVector_GUID *iface, UINT32 index )
{
    FIXME( "(%p, %u): stub!\n", iface, index );
    return E_NOTIMPL;
}

static HRESULT WINAPI uuid_vector_Append( IVector_GUID *iface, GUID value )
{
    FIXME( "(%p, %s): stub!\n", iface, debugstr_guid( &value ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI uuid_vector_RemoveAtEnd( IVector_GUID *iface )
{
    FIXME( "(%p): stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI uuid_vector_Clear( IVector_GUID *iface )
{
    FIXME( "(%p): stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI uuid_vector_GetMany( IVector_GUID *iface, UINT32 start_index, UINT32 items_size,
                                           GUID *items, UINT32 *count )
{
    struct uuid_vector *impl = impl_from_IVector_GUID( iface );
    UINT32 i;

    if (start_index > impl->count) return E_BOUNDS;
    for (i = start_index; i < impl->count && i - start_index < items_size; i++)
        items[i - start_index] = impl->uuids[i];
    *count = i - start_index;
    return S_OK;
}

static HRESULT WINAPI uuid_vector_ReplaceAll( IVector_GUID *iface, UINT32 count, GUID *items )
{
    FIXME( "(%p, %u, %p): stub!\n", iface, count, items );
    return E_NOTIMPL;
}

static const IVector_GUIDVtbl uuid_vector_vtbl =
{
    /* IUnknown */
    uuid_vector_QueryInterface,
    uuid_vector_AddRef,
    uuid_vector_Release,
    /* IInspectable */
    uuid_vector_GetIids,
    uuid_vector_GetRuntimeClassName,
    uuid_vector_GetTrustLevel,
    /* IVector<GUID> */
    uuid_vector_GetAt,
    uuid_vector_get_Size,
    uuid_vector_GetView,
    uuid_vector_IndexOf,
    uuid_vector_SetAt,
    uuid_vector_InsertAt,
    uuid_vector_RemoveAt,
    uuid_vector_Append,
    uuid_vector_RemoveAtEnd,
    uuid_vector_Clear,
    uuid_vector_GetMany,
    uuid_vector_ReplaceAll,
};

DEFINE_IINSPECTABLE( uuid_vector_view, IVectorView_GUID, struct uuid_vector, IVector_GUID_iface )

static HRESULT WINAPI uuid_vector_view_GetAt( IVectorView_GUID *iface, UINT32 index, GUID *value )
{
    struct uuid_vector *impl = impl_from_IVectorView_GUID( iface );
    return uuid_vector_GetAt( &impl->IVector_GUID_iface, index, value );
}

static HRESULT WINAPI uuid_vector_view_get_Size( IVectorView_GUID *iface, UINT32 *value )
{
    struct uuid_vector *impl = impl_from_IVectorView_GUID( iface );
    return uuid_vector_get_Size( &impl->IVector_GUID_iface, value );
}

static HRESULT WINAPI uuid_vector_view_IndexOf( IVectorView_GUID *iface, GUID element, UINT32 *index, BOOLEAN *found )
{
    struct uuid_vector *impl = impl_from_IVectorView_GUID( iface );
    return uuid_vector_IndexOf( &impl->IVector_GUID_iface, element, index, found );
}

static HRESULT WINAPI uuid_vector_view_GetMany( IVectorView_GUID *iface, UINT32 start_index, UINT32 items_size,
                                                GUID *items, UINT32 *count )
{
    struct uuid_vector *impl = impl_from_IVectorView_GUID( iface );
    return uuid_vector_GetMany( &impl->IVector_GUID_iface, start_index, items_size, items, count );
}

static const IVectorView_GUIDVtbl uuid_vector_view_vtbl =
{
    /* IUnknown */
    uuid_vector_view_QueryInterface,
    uuid_vector_view_AddRef,
    uuid_vector_view_Release,
    /* IInspectable */
    uuid_vector_view_GetIids,
    uuid_vector_view_GetRuntimeClassName,
    uuid_vector_view_GetTrustLevel,
    /* IVectorView<GUID> */
    uuid_vector_view_GetAt,
    uuid_vector_view_get_Size,
    uuid_vector_view_IndexOf,
    uuid_vector_view_GetMany,
};

static HRESULT uuid_vector_create( const GUID *uuids, UINT32 count, IVector_GUID **out )
{
    struct uuid_vector *impl;

    if (!(impl = calloc( 1, offsetof( struct uuid_vector, uuids[count] ) ))) return E_OUTOFMEMORY;
    impl->IVector_GUID_iface.lpVtbl = &uuid_vector_vtbl;
    impl->IVectorView_GUID_iface.lpVtbl = &uuid_vector_view_vtbl;
    impl->ref = 1;
    impl->count = count;
    if (count) memcpy( impl->uuids, uuids, count * sizeof( *uuids ) );
    *out = &impl->IVector_GUID_iface;
    return S_OK;
}

static const struct vector_iids data_section_vector_iids =
{
    .iterable = &IID_IIterable_BluetoothLEAdvertisementDataSection,
    .iterator = &IID_IIterator_BluetoothLEAdvertisementDataSection,
    .vector = &IID_IVector_BluetoothLEAdvertisementDataSection,
    .view = &IID_IVectorView_BluetoothLEAdvertisementDataSection,
};

static const struct vector_iids manufacturer_data_vector_iids =
{
    .iterable = &IID_IIterable_BluetoothLEManufacturerData,
    .iterator = &IID_IIterator_BluetoothLEManufacturerData,
    .vector = &IID_IVector_BluetoothLEManufacturerData,
    .view = &IID_IVectorView_BluetoothLEManufacturerData,
};

struct advertisement
{
    IBluetoothLEAdvertisement IBluetoothLEAdvertisement_iface;
    LONG ref;

    HSTRING local_name;
    IReference_BluetoothLEAdvertisementFlags *flags;
    IVector_GUID *service_uuids;
    IVector_BluetoothLEManufacturerData *manufacturer_data;
    IVector_BluetoothLEAdvertisementDataSection *sections;
};

static inline struct advertisement *impl_from_IBluetoothLEAdvertisement( IBluetoothLEAdvertisement *iface )
{
    return CONTAINING_RECORD( iface, struct advertisement, IBluetoothLEAdvertisement_iface );
}

static HRESULT WINAPI advertisement_QueryInterface( IBluetoothLEAdvertisement *iface, REFIID iid, void **out )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisement ))
    {
        IBluetoothLEAdvertisement_AddRef(( *out = &impl->IBluetoothLEAdvertisement_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI advertisement_AddRef( IBluetoothLEAdvertisement *iface )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI advertisement_Release( IBluetoothLEAdvertisement *iface )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        WindowsDeleteString( impl->local_name );
        if (impl->flags) IReference_BluetoothLEAdvertisementFlags_Release( impl->flags );
        if (impl->service_uuids) IVector_GUID_Release( impl->service_uuids );
        if (impl->manufacturer_data) IVector_BluetoothLEManufacturerData_Release( impl->manufacturer_data );
        if (impl->sections) IVector_BluetoothLEAdvertisementDataSection_Release( impl->sections );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( advertisement, IBluetoothLEAdvertisement, RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisement )

static HRESULT WINAPI advertisement_get_Flags( IBluetoothLEAdvertisement *iface, IReference_BluetoothLEAdvertisementFlags **value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->flags)) IReference_BluetoothLEAdvertisementFlags_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI advertisement_put_Flags( IBluetoothLEAdvertisement *iface, IReference_BluetoothLEAdvertisementFlags *value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (value) IReference_BluetoothLEAdvertisementFlags_AddRef( value );
    if (impl->flags) IReference_BluetoothLEAdvertisementFlags_Release( impl->flags );
    impl->flags = value;
    return S_OK;
}

static HRESULT WINAPI advertisement_get_LocalName( IBluetoothLEAdvertisement *iface, HSTRING *value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->local_name, value );
}

static HRESULT WINAPI advertisement_put_LocalName( IBluetoothLEAdvertisement *iface, HSTRING value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );
    HSTRING name;
    HRESULT hr;

    TRACE( "(%p, %s)\n", iface, debugstr_hstring( value ) );

    if (FAILED(hr = WindowsDuplicateString( value, &name ))) return hr;
    WindowsDeleteString( impl->local_name );
    impl->local_name = name;
    return S_OK;
}

static HRESULT WINAPI advertisement_get_ServiceUuids( IBluetoothLEAdvertisement *iface, IVector_GUID **value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IVector_GUID_AddRef(( *value = impl->service_uuids ));
    return S_OK;
}

static HRESULT WINAPI advertisement_get_ManufacturerData( IBluetoothLEAdvertisement *iface, IVector_BluetoothLEManufacturerData **value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IVector_BluetoothLEManufacturerData_AddRef(( *value = impl->manufacturer_data ));
    return S_OK;
}

static HRESULT WINAPI advertisement_get_DataSections( IBluetoothLEAdvertisement *iface, IVector_BluetoothLEAdvertisementDataSection **value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IVector_BluetoothLEAdvertisementDataSection_AddRef(( *value = impl->sections ));
    return S_OK;
}

static HRESULT WINAPI advertisement_GetManufacturerDataByCompanyId( IBluetoothLEAdvertisement *iface, UINT16 id,
                                                                    IVectorView_BluetoothLEManufacturerData **value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );
    IVector_BluetoothLEManufacturerData *matches;
    UINT32 i, count;
    HRESULT hr;

    TRACE( "(%p, %#x, %p)\n", iface, id, value );

    if (!value) return E_POINTER;
    if (FAILED(hr = vector_create( &manufacturer_data_vector_iids, (void **)&matches ))) return hr;
    IVector_BluetoothLEManufacturerData_get_Size( impl->manufacturer_data, &count );
    for (i = 0; i < count && SUCCEEDED(hr); i++)
    {
        IBluetoothLEManufacturerData *data;
        UINT16 company;

        if (FAILED(hr = IVector_BluetoothLEManufacturerData_GetAt( impl->manufacturer_data, i, &data ))) break;
        IBluetoothLEManufacturerData_get_CompanyId( data, &company );
        if (company == id) hr = IVector_BluetoothLEManufacturerData_Append( matches, data );
        IBluetoothLEManufacturerData_Release( data );
    }
    if (SUCCEEDED(hr)) hr = IVector_BluetoothLEManufacturerData_GetView( matches, value );
    IVector_BluetoothLEManufacturerData_Release( matches );
    return hr;
}

static HRESULT WINAPI advertisement_GetSectionsByType( IBluetoothLEAdvertisement *iface, BYTE type,
                                                       IVectorView_BluetoothLEAdvertisementDataSection **value )
{
    struct advertisement *impl = impl_from_IBluetoothLEAdvertisement( iface );
    IVector_BluetoothLEAdvertisementDataSection *matches;
    UINT32 i, count;
    HRESULT hr;

    TRACE( "(%p, %#x, %p)\n", iface, type, value );

    if (!value) return E_POINTER;
    if (FAILED(hr = vector_create( &data_section_vector_iids, (void **)&matches ))) return hr;
    IVector_BluetoothLEAdvertisementDataSection_get_Size( impl->sections, &count );
    for (i = 0; i < count && SUCCEEDED(hr); i++)
    {
        IBluetoothLEAdvertisementDataSection *section;
        BYTE section_type;

        if (FAILED(hr = IVector_BluetoothLEAdvertisementDataSection_GetAt( impl->sections, i, &section ))) break;
        IBluetoothLEAdvertisementDataSection_get_DataType( section, &section_type );
        if (section_type == type) hr = IVector_BluetoothLEAdvertisementDataSection_Append( matches, section );
        IBluetoothLEAdvertisementDataSection_Release( section );
    }
    if (SUCCEEDED(hr)) hr = IVector_BluetoothLEAdvertisementDataSection_GetView( matches, value );
    IVector_BluetoothLEAdvertisementDataSection_Release( matches );
    return hr;
}

static const IBluetoothLEAdvertisementVtbl advertisement_vtbl =
{
    /* IUnknown */
    advertisement_QueryInterface,
    advertisement_AddRef,
    advertisement_Release,
    /* IInspectable */
    advertisement_GetIids,
    advertisement_GetRuntimeClassName,
    advertisement_GetTrustLevel,
    /* IBluetoothLEAdvertisement */
    advertisement_get_Flags,
    advertisement_put_Flags,
    advertisement_get_LocalName,
    advertisement_put_LocalName,
    advertisement_get_ServiceUuids,
    advertisement_get_ManufacturerData,
    advertisement_get_DataSections,
    advertisement_GetManufacturerDataByCompanyId,
    advertisement_GetSectionsByType,
};

static void uuid_from_adv_bytes( const BYTE *bytes, UINT32 size, GUID *uuid )
{
    static const GUID base = { 0, 0, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
    int i;

    if (size == 2)
    {
        *uuid = base;
        uuid->Data1 = bytes[0] | (bytes[1] << 8);
        return;
    }
    for (i = 0; i < 8; i++) uuid->Data4[7 - i] = bytes[i];
    uuid->Data3 = bytes[8] | (bytes[9] << 8);
    uuid->Data2 = bytes[10] | (bytes[11] << 8);
    uuid->Data1 = bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | ((UINT32)bytes[15] << 24);
}

/* Create an advertisement from a sequence of advertising data structures. */
static HRESULT advertisement_create( const BYTE *data, UINT32 size, IBluetoothLEAdvertisement **out )
{
    struct advertisement *impl;
    UINT32 pos = 0, uuid_count = 0;
    GUID uuids[64];
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IBluetoothLEAdvertisement_iface.lpVtbl = &advertisement_vtbl;
    impl->ref = 1;

    if (FAILED(hr = vector_create( &data_section_vector_iids, (void **)&impl->sections ))) goto failed;
    if (FAILED(hr = vector_create( &manufacturer_data_vector_iids, (void **)&impl->manufacturer_data ))) goto failed;

    while (pos + 2 <= size && data[pos])
    {
        IBluetoothLEAdvertisementDataSection *section;
        const BYTE *payload = data + pos + 2;
        UINT32 len = data[pos] - 1, i;
        BYTE type = data[pos + 1];

        if (pos + 2 + len > size) break;
        pos += 2 + len;

        if (FAILED(hr = data_section_create( type, payload, len, &section ))) goto failed;
        hr = IVector_BluetoothLEAdvertisementDataSection_Append( impl->sections, section );
        IBluetoothLEAdvertisementDataSection_Release( section );
        if (FAILED(hr)) goto failed;

        switch (type)
        {
        case AD_TYPE_FLAGS:
        {
            BluetoothLEAdvertisementFlags flags;

            if (!len || impl->flags) break;
            flags = payload[0];
            if (FAILED(hr = reference_create( &IID_IReference_BluetoothLEAdvertisementFlags, &flags, sizeof( flags ),
                                              (IInspectable **)&impl->flags )))
                goto failed;
            break;
        }
        case AD_TYPE_SHORTENED_LOCAL_NAME:
        case AD_TYPE_COMPLETE_LOCAL_NAME:
        {
            WCHAR name[256];
            int name_len;

            if (impl->local_name && type == AD_TYPE_SHORTENED_LOCAL_NAME) break;
            name_len = MultiByteToWideChar( CP_UTF8, 0, (const char *)payload, len, name, ARRAY_SIZE( name ) );
            WindowsDeleteString( impl->local_name );
            impl->local_name = NULL;
            if (FAILED(hr = WindowsCreateString( name, name_len, &impl->local_name ))) goto failed;
            break;
        }
        case AD_TYPE_INCOMPLETE_16BIT_UUIDS:
        case AD_TYPE_COMPLETE_16BIT_UUIDS:
            for (i = 0; i + 2 <= len && uuid_count < ARRAY_SIZE( uuids ); i += 2)
                uuid_from_adv_bytes( payload + i, 2, &uuids[uuid_count++] );
            break;
        case AD_TYPE_INCOMPLETE_128BIT_UUIDS:
        case AD_TYPE_COMPLETE_128BIT_UUIDS:
            for (i = 0; i + 16 <= len && uuid_count < ARRAY_SIZE( uuids ); i += 16)
                uuid_from_adv_bytes( payload + i, 16, &uuids[uuid_count++] );
            break;
        case AD_TYPE_MANUFACTURER_DATA:
        {
            IBluetoothLEManufacturerData *manufacturer_data;

            if (len < 2) break;
            if (FAILED(hr = manufacturer_data_create( payload[0] | (payload[1] << 8), payload + 2, len - 2,
                                                      &manufacturer_data )))
                goto failed;
            hr = IVector_BluetoothLEManufacturerData_Append( impl->manufacturer_data, manufacturer_data );
            IBluetoothLEManufacturerData_Release( manufacturer_data );
            if (FAILED(hr)) goto failed;
            break;
        }
        }
    }

    if (FAILED(hr = uuid_vector_create( uuids, uuid_count, &impl->service_uuids ))) goto failed;

    *out = &impl->IBluetoothLEAdvertisement_iface;
    return S_OK;

failed:
    IBluetoothLEAdvertisement_Release( &impl->IBluetoothLEAdvertisement_iface );
    return hr;
}

struct received_args
{
    IBluetoothLEAdvertisementReceivedEventArgs IBluetoothLEAdvertisementReceivedEventArgs_iface;
    IBluetoothLEAdvertisementReceivedEventArgs2 IBluetoothLEAdvertisementReceivedEventArgs2_iface;
    LONG ref;

    UINT64 address;
    BOOL random_address;
    INT16 rssi;
    DateTime timestamp;
    IBluetoothLEAdvertisement *advertisement;
    IReference_INT16 *tx_power;
};

static inline struct received_args *impl_from_IBluetoothLEAdvertisementReceivedEventArgs( IBluetoothLEAdvertisementReceivedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct received_args, IBluetoothLEAdvertisementReceivedEventArgs_iface );
}

static HRESULT WINAPI received_args_QueryInterface( IBluetoothLEAdvertisementReceivedEventArgs *iface, REFIID iid, void **out )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementReceivedEventArgs ))
    {
        IBluetoothLEAdvertisementReceivedEventArgs_AddRef(( *out = &impl->IBluetoothLEAdvertisementReceivedEventArgs_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementReceivedEventArgs2 ))
    {
        IBluetoothLEAdvertisementReceivedEventArgs2_AddRef(( *out = &impl->IBluetoothLEAdvertisementReceivedEventArgs2_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI received_args_AddRef( IBluetoothLEAdvertisementReceivedEventArgs *iface )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI received_args_Release( IBluetoothLEAdvertisementReceivedEventArgs *iface )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        if (impl->advertisement) IBluetoothLEAdvertisement_Release( impl->advertisement );
        if (impl->tx_power) IReference_INT16_Release( impl->tx_power );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( received_args, IBluetoothLEAdvertisementReceivedEventArgs, RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementReceivedEventArgs )

static HRESULT WINAPI received_args_get_RawSignalStrengthInDBm( IBluetoothLEAdvertisementReceivedEventArgs *iface, INT16 *value )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->rssi;
    return S_OK;
}

static HRESULT WINAPI received_args_get_BluetoothAddress( IBluetoothLEAdvertisementReceivedEventArgs *iface, UINT64 *value )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->address;
    return S_OK;
}

static HRESULT WINAPI received_args_get_AdvertisementType( IBluetoothLEAdvertisementReceivedEventArgs *iface,
                                                           BluetoothLEAdvertisementType *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    /* BlueZ merges advertisements with their scan responses, and doesn't tell their type. */
    *value = BluetoothLEAdvertisementType_ConnectableUndirected;
    return S_OK;
}

static HRESULT WINAPI received_args_get_Timestamp( IBluetoothLEAdvertisementReceivedEventArgs *iface, DateTime *value )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->timestamp;
    return S_OK;
}

static HRESULT WINAPI received_args_get_Advertisement( IBluetoothLEAdvertisementReceivedEventArgs *iface,
                                                       IBluetoothLEAdvertisement **value )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IBluetoothLEAdvertisement_AddRef(( *value = impl->advertisement ));
    return S_OK;
}

static const IBluetoothLEAdvertisementReceivedEventArgsVtbl received_args_vtbl =
{
    /* IUnknown */
    received_args_QueryInterface,
    received_args_AddRef,
    received_args_Release,
    /* IInspectable */
    received_args_GetIids,
    received_args_GetRuntimeClassName,
    received_args_GetTrustLevel,
    /* IBluetoothLEAdvertisementReceivedEventArgs */
    received_args_get_RawSignalStrengthInDBm,
    received_args_get_BluetoothAddress,
    received_args_get_AdvertisementType,
    received_args_get_Timestamp,
    received_args_get_Advertisement,
};

DEFINE_IINSPECTABLE( received_args2, IBluetoothLEAdvertisementReceivedEventArgs2, struct received_args,
                     IBluetoothLEAdvertisementReceivedEventArgs_iface )

static HRESULT WINAPI received_args2_get_BluetoothAddressType( IBluetoothLEAdvertisementReceivedEventArgs2 *iface,
                                                               BluetoothAddressType *value )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs2( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->random_address ? BluetoothAddressType_Random : BluetoothAddressType_Public;
    return S_OK;
}

static HRESULT WINAPI received_args2_get_TransmitPowerLevelInDBm( IBluetoothLEAdvertisementReceivedEventArgs2 *iface,
                                                                  IReference_INT16 **value )
{
    struct received_args *impl = impl_from_IBluetoothLEAdvertisementReceivedEventArgs2( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->tx_power)) IReference_INT16_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI received_args2_get_IsAnonymous( IBluetoothLEAdvertisementReceivedEventArgs2 *iface, boolean *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI received_args2_get_IsConnectable( IBluetoothLEAdvertisementReceivedEventArgs2 *iface, boolean *value )
{
    FIXME( "(%p, %p): semi-stub!\n", iface, value );

    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI received_args2_get_IsScannable( IBluetoothLEAdvertisementReceivedEventArgs2 *iface, boolean *value )
{
    FIXME( "(%p, %p): semi-stub!\n", iface, value );

    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI received_args2_get_IsDirected( IBluetoothLEAdvertisementReceivedEventArgs2 *iface, boolean *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI received_args2_get_IsScanResponse( IBluetoothLEAdvertisementReceivedEventArgs2 *iface, boolean *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const IBluetoothLEAdvertisementReceivedEventArgs2Vtbl received_args2_vtbl =
{
    /* IUnknown */
    received_args2_QueryInterface,
    received_args2_AddRef,
    received_args2_Release,
    /* IInspectable */
    received_args2_GetIids,
    received_args2_GetRuntimeClassName,
    received_args2_GetTrustLevel,
    /* IBluetoothLEAdvertisementReceivedEventArgs2 */
    received_args2_get_BluetoothAddressType,
    received_args2_get_TransmitPowerLevelInDBm,
    received_args2_get_IsAnonymous,
    received_args2_get_IsConnectable,
    received_args2_get_IsScannable,
    received_args2_get_IsDirected,
    received_args2_get_IsScanResponse,
};

static HRESULT received_args_create( const struct winebth_radio_le_advertisement *adv,
                                     IBluetoothLEAdvertisementReceivedEventArgs **out )
{
    struct received_args *impl;
    FILETIME time;
    UINT32 pos;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IBluetoothLEAdvertisementReceivedEventArgs_iface.lpVtbl = &received_args_vtbl;
    impl->IBluetoothLEAdvertisementReceivedEventArgs2_iface.lpVtbl = &received_args2_vtbl;
    impl->ref = 1;
    impl->address = adv->address;
    impl->random_address = adv->random_address;
    impl->rssi = adv->rssi;
    GetSystemTimeAsFileTime( &time );
    impl->timestamp.UniversalTime = ((INT64)time.dwHighDateTime << 32) | time.dwLowDateTime;

    hr = advertisement_create( adv->data, adv->data_size, &impl->advertisement );
    for (pos = 0; SUCCEEDED(hr) && pos + 2 < adv->data_size && adv->data[pos]; pos += adv->data[pos] + 1)
    {
        INT16 tx_power;

        if (adv->data[pos + 1] != AD_TYPE_TX_POWER_LEVEL) continue;
        tx_power = (INT8)adv->data[pos + 2];
        hr = reference_create( &IID_IReference_INT16, &tx_power, sizeof( tx_power ), (IInspectable **)&impl->tx_power );
        break;
    }
    if (FAILED(hr))
    {
        IBluetoothLEAdvertisementReceivedEventArgs_Release( &impl->IBluetoothLEAdvertisementReceivedEventArgs_iface );
        return hr;
    }

    *out = &impl->IBluetoothLEAdvertisementReceivedEventArgs_iface;
    return S_OK;
}

struct stopped_args
{
    IBluetoothLEAdvertisementWatcherStoppedEventArgs IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface;
    LONG ref;

    BluetoothError error;
};

static inline struct stopped_args *impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct stopped_args, IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface );
}

static HRESULT WINAPI stopped_args_QueryInterface( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, REFIID iid, void **out )
{
    struct stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementWatcherStoppedEventArgs ))
    {
        IBluetoothLEAdvertisementWatcherStoppedEventArgs_AddRef(( *out = &impl->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI stopped_args_AddRef( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface )
{
    struct stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI stopped_args_Release( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface )
{
    struct stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( stopped_args, IBluetoothLEAdvertisementWatcherStoppedEventArgs, RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementWatcherStoppedEventArgs )

static HRESULT WINAPI stopped_args_get_Error( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, BluetoothError *value )
{
    struct stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->error;
    return S_OK;
}

static const IBluetoothLEAdvertisementWatcherStoppedEventArgsVtbl stopped_args_vtbl =
{
    /* IUnknown */
    stopped_args_QueryInterface,
    stopped_args_AddRef,
    stopped_args_Release,
    /* IInspectable */
    stopped_args_GetIids,
    stopped_args_GetRuntimeClassName,
    stopped_args_GetTrustLevel,
    /* IBluetoothLEAdvertisementWatcherStoppedEventArgs */
    stopped_args_get_Error,
};

static HRESULT stopped_args_create( BluetoothError error, IBluetoothLEAdvertisementWatcherStoppedEventArgs **out )
{
    struct stopped_args *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface.lpVtbl = &stopped_args_vtbl;
    impl->ref = 1;
    impl->error = error;
    *out = &impl->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface;
    return S_OK;
}

struct adv_watcher
{
    IBluetoothLEAdvertisementWatcher IBluetoothLEAdvertisementWatcher_iface;
    IBluetoothLEAdvertisementWatcher2 IBluetoothLEAdvertisementWatcher2_iface;
    LONG ref;

    CRITICAL_SECTION cs;
    BluetoothLEAdvertisementWatcherStatus status;
    BluetoothLEScanningMode mode;
    boolean allow_extended;
    IBluetoothSignalStrengthFilter *signal_filter;
    IBluetoothLEAdvertisementFilter *adv_filter;
    HANDLE thread;
    DWORD thread_id;
    HANDLE stop_event;
    BOOL detached; /* Whether the scanning thread is the one that released the last reference. */

    struct list received_handlers;
    struct list stopped_handlers;
};

static void adv_watcher_destroy( struct adv_watcher *impl )
{
    if (impl->thread) CloseHandle( impl->thread );
    CloseHandle( impl->stop_event );
    if (impl->signal_filter) IBluetoothSignalStrengthFilter_Release( impl->signal_filter );
    if (impl->adv_filter) IBluetoothLEAdvertisementFilter_Release( impl->adv_filter );
    event_handlers_clear( &impl->received_handlers );
    event_handlers_clear( &impl->stopped_handlers );
    impl->cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection( &impl->cs );
    free( impl );
}

/* Take a reference, unless the watcher is being destroyed. */
static BOOL adv_watcher_try_addref( struct adv_watcher *impl )
{
    LONG ref;

    do
    {
        if (!(ref = impl->ref)) return FALSE;
    } while (InterlockedCompareExchange( &impl->ref, ref + 1, ref ) != ref);
    return TRUE;
}

static inline struct adv_watcher *impl_from_IBluetoothLEAdvertisementWatcher( IBluetoothLEAdvertisementWatcher *iface )
{
    return CONTAINING_RECORD( iface, struct adv_watcher, IBluetoothLEAdvertisementWatcher_iface );
}

static HRESULT WINAPI adv_watcher_QueryInterface( IBluetoothLEAdvertisementWatcher *iface, REFIID iid, void **out )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementWatcher ))
    {
        IBluetoothLEAdvertisementWatcher_AddRef(( *out = &impl->IBluetoothLEAdvertisementWatcher_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementWatcher2 ))
    {
        IBluetoothLEAdvertisementWatcher2_AddRef(( *out = &impl->IBluetoothLEAdvertisementWatcher2_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI adv_watcher_AddRef( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI adv_watcher_Release( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        /* A watcher that is released while it is still scanning gets stopped. */
        SetEvent( impl->stop_event );
        if (impl->thread && GetCurrentThreadId() == impl->thread_id)
            impl->detached = TRUE;
        else
        {
            if (impl->thread) WaitForSingleObject( impl->thread, INFINITE );
            adv_watcher_destroy( impl );
        }
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( adv_watcher, IBluetoothLEAdvertisementWatcher, RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementWatcher )

static HRESULT WINAPI adv_watcher_get_MinSamplingInternal( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 1000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MaxSamplingInternal( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 255000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MinOutOfRangeTimeout( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 10000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MaxOutOfRangeTimeout( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 600000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_Status( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEAdvertisementWatcherStatus *status )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p)\n", iface, status );

    if (!status) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *status = impl->status;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_ScanningMode( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEScanningMode *value )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->mode;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_put_ScanningMode( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEScanningMode value )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %d)\n", iface, value );

    /* BlueZ always scans actively. */
    impl->mode = value;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_SignalStrengthFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothSignalStrengthFilter **filter )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    FIXME( "(%p, %p): semi-stub!\n", iface, filter );

    if (!filter) return E_POINTER;
    if ((*filter = impl->signal_filter)) IBluetoothSignalStrengthFilter_AddRef( *filter );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_put_SignalStrengthFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothSignalStrengthFilter *filter )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    FIXME( "(%p, %p): semi-stub!\n", iface, filter );

    /* TODO: apply the filter to the advertisements that get reported. */
    if (filter) IBluetoothSignalStrengthFilter_AddRef( filter );
    if (impl->signal_filter) IBluetoothSignalStrengthFilter_Release( impl->signal_filter );
    impl->signal_filter = filter;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_AdvertisementFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothLEAdvertisementFilter **filter )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    FIXME( "(%p, %p): semi-stub!\n", iface, filter );

    if (!filter) return E_POINTER;
    if ((*filter = impl->adv_filter)) IBluetoothLEAdvertisementFilter_AddRef( *filter );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_put_AdvertisementFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothLEAdvertisementFilter *filter )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    FIXME( "(%p, %p): semi-stub!\n", iface, filter );

    /* TODO: apply the filter to the advertisements that get reported. */
    if (filter) IBluetoothLEAdvertisementFilter_AddRef( filter );
    if (impl->adv_filter) IBluetoothLEAdvertisementFilter_Release( impl->adv_filter );
    impl->adv_filter = filter;
    return S_OK;
}

static BOOL radio_ioctl( HANDLE radio, DWORD code, OVERLAPPED *ovl )
{
    DWORD bytes;

    if (DeviceIoControl( radio, code, NULL, 0, NULL, 0, &bytes, ovl )) return TRUE;
    return GetLastError() == ERROR_IO_PENDING && GetOverlappedResult( radio, ovl, &bytes, TRUE );
}

static DWORD WINAPI adv_watcher_thread( void *arg )
{
    BYTE buffer[offsetof( struct winebth_radio_le_advertisement, data[WINEBTH_LE_ADVERTISEMENT_MAX_SIZE] )];
    struct winebth_radio_le_advertisement *adv = (struct winebth_radio_le_advertisement *)buffer;
    IBluetoothLEAdvertisementWatcherStoppedEventArgs *stopped_args;
    BluetoothError error = BluetoothError_Success;
    HANDLE radio, handles[2];
    struct adv_watcher *impl = arg;
    OVERLAPPED ovl = {0};
    DWORD bytes;

    SetThreadDescription( GetCurrentThread(), L"wine_bluetooth_adv_watcher" );
    RoInitialize( RO_INIT_MULTITHREADED );

    ovl.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    handles[0] = impl->stop_event;
    handles[1] = ovl.hEvent;

    radio = open_bluetooth_radio( FILE_FLAG_OVERLAPPED );
    if (radio == INVALID_HANDLE_VALUE)
    {
        WARN( "No Bluetooth radio available.\n" );
        error = BluetoothError_RadioNotAvailable;
        goto done;
    }
    if (!radio_ioctl( radio, IOCTL_WINEBTH_RADIO_START_LE_SCAN, &ovl ))
    {
        WARN( "Failed to start scanning: %lu\n", GetLastError() );
        error = BluetoothError_RadioNotAvailable;
        goto done;
    }

    for (;;)
    {
        IBluetoothLEAdvertisementReceivedEventArgs *args;

        if (!DeviceIoControl( radio, IOCTL_WINEBTH_RADIO_READ_LE_ADVERTISEMENT, NULL, 0, buffer, sizeof( buffer ),
                              &bytes, &ovl ))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                WARN( "Failed to read advertisement: %lu\n", GetLastError() );
                error = BluetoothError_OtherError;
                break;
            }
            if (WaitForMultipleObjects( 2, handles, FALSE, INFINITE ) == WAIT_OBJECT_0)
            {
                CancelIoEx( radio, &ovl );
                GetOverlappedResult( radio, &ovl, &bytes, TRUE );
                break;
            }
            if (!GetOverlappedResult( radio, &ovl, &bytes, FALSE ))
            {
                WARN( "Failed to read advertisement: %lu\n", GetLastError() );
                error = BluetoothError_OtherError;
                break;
            }
        }
        if (WaitForSingleObject( impl->stop_event, 0 ) == WAIT_OBJECT_0) break;
        if (bytes < offsetof( struct winebth_radio_le_advertisement, data ) ||
            bytes < offsetof( struct winebth_radio_le_advertisement, data[adv->data_size] ))
            continue;

        TRACE( "Advertisement from %I64x, rssi %d, %lu bytes\n", adv->address, adv->rssi, adv->data_size );
        if (FAILED(received_args_create( adv, &args ))) continue;
        if (adv_watcher_try_addref( impl ))
        {
            event_handlers_notify( &impl->received_handlers, (IInspectable *)&impl->IBluetoothLEAdvertisementWatcher_iface,
                                   (IInspectable *)args );
            IBluetoothLEAdvertisementWatcher_Release( &impl->IBluetoothLEAdvertisementWatcher_iface );
        }
        IBluetoothLEAdvertisementReceivedEventArgs_Release( args );
    }

done:
    /* Report that the watcher has stopped right away, stopping the scan itself can take a while. */
    if (adv_watcher_try_addref( impl ))
    {
        EnterCriticalSection( &impl->cs );
        impl->status = error ? BluetoothLEAdvertisementWatcherStatus_Aborted : BluetoothLEAdvertisementWatcherStatus_Stopped;
        LeaveCriticalSection( &impl->cs );

        if (SUCCEEDED(stopped_args_create( error, &stopped_args )))
        {
            event_handlers_notify( &impl->stopped_handlers, (IInspectable *)&impl->IBluetoothLEAdvertisementWatcher_iface,
                                   (IInspectable *)stopped_args );
            IBluetoothLEAdvertisementWatcherStoppedEventArgs_Release( stopped_args );
        }
        IBluetoothLEAdvertisementWatcher_Release( &impl->IBluetoothLEAdvertisementWatcher_iface );
    }

    if (radio != INVALID_HANDLE_VALUE)
    {
        radio_ioctl( radio, IOCTL_WINEBTH_RADIO_STOP_LE_SCAN, &ovl );
        CloseHandle( radio );
    }
    CloseHandle( ovl.hEvent );

    RoUninitialize();
    /* The last reference was released from one of the handlers called above. */
    if (impl->detached) adv_watcher_destroy( impl );
    return 0;
}

static HRESULT WINAPI adv_watcher_Start( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    HANDLE old_thread;
    HRESULT hr = S_OK;

    TRACE( "(%p)\n", iface );

    EnterCriticalSection( &impl->cs );
    if (impl->status == BluetoothLEAdvertisementWatcherStatus_Started)
    {
        LeaveCriticalSection( &impl->cs );
        return S_OK;
    }
    if (impl->status == BluetoothLEAdvertisementWatcherStatus_Stopping)
    {
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_METHOD_CALL;
    }
    old_thread = impl->thread;
    impl->thread = NULL;
    LeaveCriticalSection( &impl->cs );

    if (old_thread)
    {
        /* The thread has reported that it stopped, but may still be exiting. */
        if (GetCurrentThreadId() != impl->thread_id) WaitForSingleObject( old_thread, INFINITE );
        CloseHandle( old_thread );
    }

    EnterCriticalSection( &impl->cs );
    ResetEvent( impl->stop_event );
    impl->status = BluetoothLEAdvertisementWatcherStatus_Started;
    if (!(impl->thread = CreateThread( NULL, 0, adv_watcher_thread, impl, 0, &impl->thread_id )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        impl->status = BluetoothLEAdvertisementWatcherStatus_Aborted;
    }
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI adv_watcher_Stop( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p)\n", iface );

    EnterCriticalSection( &impl->cs );
    if (impl->status == BluetoothLEAdvertisementWatcherStatus_Started)
    {
        impl->status = BluetoothLEAdvertisementWatcherStatus_Stopping;
        SetEvent( impl->stop_event );
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_add_Received( IBluetoothLEAdvertisementWatcher *iface,
                                                ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementReceivedEventArgs *handler,
                                                EventRegistrationToken *token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    if (!token) return E_POINTER;
    return event_handlers_append( &impl->received_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI adv_watcher_remove_Received( IBluetoothLEAdvertisementWatcher *iface, EventRegistrationToken token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %I64x)\n", iface, token.value );

    return event_handlers_remove( &impl->received_handlers, &token );
}

static HRESULT WINAPI adv_watcher_add_Stopped( IBluetoothLEAdvertisementWatcher *iface,
                                               ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementWatcherStoppedEventArgs *handler,
                                               EventRegistrationToken *token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    if (!token) return E_POINTER;
    return event_handlers_append( &impl->stopped_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI adv_watcher_remove_Stopped( IBluetoothLEAdvertisementWatcher *iface, EventRegistrationToken token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %I64x)\n", iface, token.value );

    return event_handlers_remove( &impl->stopped_handlers, &token );
}

static const IBluetoothLEAdvertisementWatcherVtbl adv_watcher_vtbl =
{
    /* IUnknown */
    adv_watcher_QueryInterface,
    adv_watcher_AddRef,
    adv_watcher_Release,
    /* IInspectable */
    adv_watcher_GetIids,
    adv_watcher_GetRuntimeClassName,
    adv_watcher_GetTrustLevel,
    /* IBluetoothLEAdvertisementWatcher */
    adv_watcher_get_MinSamplingInternal,
    adv_watcher_get_MaxSamplingInternal,
    adv_watcher_get_MinOutOfRangeTimeout,
    adv_watcher_get_MaxOutOfRangeTimeout,
    adv_watcher_get_Status,
    adv_watcher_get_ScanningMode,
    adv_watcher_put_ScanningMode,
    adv_watcher_get_SignalStrengthFilter,
    adv_watcher_put_SignalStrengthFilter,
    adv_watcher_get_AdvertisementFilter,
    adv_watcher_put_AdvertisementFilter,
    adv_watcher_Start,
    adv_watcher_Stop,
    adv_watcher_add_Received,
    adv_watcher_remove_Received,
    adv_watcher_add_Stopped,
    adv_watcher_remove_Stopped
};

DEFINE_IINSPECTABLE( adv_watcher2, IBluetoothLEAdvertisementWatcher2, struct adv_watcher,
                     IBluetoothLEAdvertisementWatcher_iface )

static HRESULT WINAPI adv_watcher2_get_AllowExtendedAdvertisements( IBluetoothLEAdvertisementWatcher2 *iface, boolean *value )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher2( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->allow_extended;
    return S_OK;
}

static HRESULT WINAPI adv_watcher2_put_AllowExtendedAdvertisements( IBluetoothLEAdvertisementWatcher2 *iface, boolean value )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher2( iface );

    TRACE( "(%p, %d)\n", iface, value );

    impl->allow_extended = value;
    return S_OK;
}

static const IBluetoothLEAdvertisementWatcher2Vtbl adv_watcher2_vtbl =
{
    /* IUnknown */
    adv_watcher2_QueryInterface,
    adv_watcher2_AddRef,
    adv_watcher2_Release,
    /* IInspectable */
    adv_watcher2_GetIids,
    adv_watcher2_GetRuntimeClassName,
    adv_watcher2_GetTrustLevel,
    /* IBluetoothLEAdvertisementWatcher2 */
    adv_watcher2_get_AllowExtendedAdvertisements,
    adv_watcher2_put_AllowExtendedAdvertisements,
};

static HRESULT adv_watcher_create( IBluetoothLEAdvertisementWatcher **watcher )
{
    struct adv_watcher *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (!(impl->stop_event = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        free( impl );
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    impl->IBluetoothLEAdvertisementWatcher_iface.lpVtbl = &adv_watcher_vtbl;
    impl->IBluetoothLEAdvertisementWatcher2_iface.lpVtbl = &adv_watcher2_vtbl;
    impl->ref = 1;
    impl->status = BluetoothLEAdvertisementWatcherStatus_Created;
    impl->mode = BluetoothLEScanningMode_Passive;
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": adv_watcher.cs");
    list_init( &impl->received_handlers );
    list_init( &impl->stopped_handlers );
    *watcher = &impl->IBluetoothLEAdvertisementWatcher_iface;
    return S_OK;
}

struct adv_watcher_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct adv_watcher_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct adv_watcher_factory, IActivationFactory_iface );
}

static HRESULT WINAPI adv_watcher_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct adv_watcher_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef(( *out = &impl->IActivationFactory_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI adv_watcher_factory_AddRef( IActivationFactory *iface )
{
    struct adv_watcher_factory *impl = impl_from_IActivationFactory( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI adv_watcher_factory_Release( IActivationFactory *iface )
{
    struct adv_watcher_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    return ref;
}

static HRESULT WINAPI adv_watcher_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "(%p, %p)\n", iface, instance );
    return adv_watcher_create( (IBluetoothLEAdvertisementWatcher **)instance );
}

static const struct IActivationFactoryVtbl adv_watcher_factory_vtbl =
{
    adv_watcher_factory_QueryInterface,
    adv_watcher_factory_AddRef,
    adv_watcher_factory_Release,
    /* IInspectable */
    adv_watcher_factory_GetIids,
    adv_watcher_factory_GetRuntimeClassName,
    adv_watcher_factory_GetTrustLevel,
    /* IActivationFactory */
    adv_watcher_factory_ActivateInstance
};

static struct adv_watcher_factory adv_watcher_factory =
{
    {&adv_watcher_factory_vtbl},
    1
};

IActivationFactory *advertisement_watcher_factory = &adv_watcher_factory.IActivationFactory_iface;
