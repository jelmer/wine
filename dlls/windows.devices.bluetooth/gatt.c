/* IGatt* Implementation
 *
 * Copyright 2026 Vibhav Pant
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
#include "bluetoothleapis.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetooth );

/* The ATT "unlikely error" code, reported when the actual error isn't known. */
#define ATT_ERROR_UNLIKELY 0x0e

static const GUID null_uuid;

static const struct vector_iids service_vector_iids =
{
    .vector = &IID_IVector_IInspectable,
    .view = &IID_IVectorView_GattDeviceService,
    .iterable = &IID_IIterable_GattDeviceService,
    .iterator = &IID_IIterator_GattDeviceService,
};

static const struct vector_iids characteristic_vector_iids =
{
    .vector = &IID_IVector_IInspectable,
    .view = &IID_IVectorView_GattCharacteristic,
    .iterable = &IID_IIterable_GattCharacteristic,
    .iterator = &IID_IIterator_GattCharacteristic,
};

static const struct vector_iids descriptor_vector_iids =
{
    .vector = &IID_IVector_IInspectable,
    .view = &IID_IVectorView_GattDescriptor,
    .iterable = &IID_IIterable_GattDescriptor,
    .iterator = &IID_IIterator_GattDescriptor,
};

static const struct vector_iids presentation_format_vector_iids =
{
    .vector = &IID_IVector_IInspectable,
    .view = &IID_IVectorView_GattPresentationFormat,
    .iterable = &IID_IIterable_GattPresentationFormat,
    .iterator = &IID_IIterator_GattPresentationFormat,
};

static HRESULT empty_view_create( const struct vector_iids *iids, void **view )
{
    IVector_IInspectable *vector;
    HRESULT hr;

    if (FAILED(hr = vector_create( iids, (void **)&vector ))) return hr;
    hr = IVector_IInspectable_GetView( vector, (IVectorView_IInspectable **)view );
    IVector_IInspectable_Release( vector );
    return hr;
}

static void le_uuid_to_guid( const BTH_LE_UUID *le_uuid, GUID *uuid )
{
    if (le_uuid->IsShortUuid)
    {
        *uuid = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
        uuid->Data1 = le_uuid->Value.ShortUuid;
    }
    else
        *uuid = le_uuid->Value.LongUuid;
}

static HRESULT protocol_error_create( GattCommunicationStatus status, IReference_BYTE **out )
{
    BYTE error = ATT_ERROR_UNLIKELY;

    *out = NULL;
    if (status != GattCommunicationStatus_ProtocolError) return S_OK;
    /* TODO: report the error that the device returned. */
    return reference_create( &IID_IReference_BYTE, &error, sizeof( error ), (IInspectable **)out );
}

/* The status for a request that failed. */
static GattCommunicationStatus gatt_status_from_error( struct ble_device *device )
{
    struct winebth_le_device_info info;

    WARN( "Request failed: %lu\n", GetLastError() );
    if (FAILED(ble_device_get_info( device, &info )) || !info.connected) return GattCommunicationStatus_Unreachable;
    return GattCommunicationStatus_ProtocolError;
}

/* Parameters for an asynchronous operation. */
struct async_params
{
    IUnknown IUnknown_iface;
    LONG ref;

    BOOL has_uuid;
    GUID uuid;
    BluetoothCacheMode mode;
    IBuffer *buffer;
    GattWriteOption option;
    GattClientCharacteristicConfigurationDescriptorValue cccd;
};

static inline struct async_params *impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct async_params, IUnknown_iface );
}

static HRESULT WINAPI async_params_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        IUnknown_AddRef( iface );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_params_AddRef( IUnknown *iface )
{
    struct async_params *impl = impl_from_IUnknown( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_params_Release( IUnknown *iface )
{
    struct async_params *impl = impl_from_IUnknown( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->buffer) IBuffer_Release( impl->buffer );
        free( impl );
    }
    return ref;
}

static const IUnknownVtbl async_params_vtbl =
{
    async_params_QueryInterface,
    async_params_AddRef,
    async_params_Release,
};

static struct async_params *async_params_create( const GUID *uuid, BluetoothCacheMode mode )
{
    struct async_params *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return NULL;
    impl->IUnknown_iface.lpVtbl = &async_params_vtbl;
    impl->ref = 1;
    if ((impl->has_uuid = !!uuid)) impl->uuid = *uuid;
    impl->mode = mode;
    return impl;
}

/* Start an operation, handing over the reference to its parameters. */
static HRESULT async_operation_start( const GUID *iid, void *invoker, struct async_params *params,
                                      async_operation_callback callback, BOOL value_result, void *out )
{
    HRESULT hr;

    if (!params) return E_OUTOFMEMORY;
    if (value_result)
        hr = async_operation_value_create( iid, invoker, &params->IUnknown_iface, callback, out );
    else
        hr = async_operation_inspectable_create( iid, invoker, &params->IUnknown_iface, callback, out );
    IUnknown_Release( &params->IUnknown_iface );
    return hr;
}

struct value_changed_args
{
    IGattValueChangedEventArgs IGattValueChangedEventArgs_iface;
    LONG ref;

    IBuffer *value;
    DateTime timestamp;
};

static inline struct value_changed_args *impl_from_IGattValueChangedEventArgs( IGattValueChangedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct value_changed_args, IGattValueChangedEventArgs_iface );
}

static HRESULT WINAPI value_changed_args_QueryInterface( IGattValueChangedEventArgs *iface, REFIID iid, void **out )
{
    struct value_changed_args *impl = impl_from_IGattValueChangedEventArgs( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattValueChangedEventArgs ))
    {
        IGattValueChangedEventArgs_AddRef(( *out = &impl->IGattValueChangedEventArgs_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI value_changed_args_AddRef( IGattValueChangedEventArgs *iface )
{
    struct value_changed_args *impl = impl_from_IGattValueChangedEventArgs( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI value_changed_args_Release( IGattValueChangedEventArgs *iface )
{
    struct value_changed_args *impl = impl_from_IGattValueChangedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        IBuffer_Release( impl->value );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( value_changed_args, IGattValueChangedEventArgs, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattValueChangedEventArgs )

static HRESULT WINAPI value_changed_args_get_CharacteristicValue( IGattValueChangedEventArgs *iface, IBuffer **value )
{
    struct value_changed_args *impl = impl_from_IGattValueChangedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IBuffer_AddRef(( *value = impl->value ));
    return S_OK;
}

static HRESULT WINAPI value_changed_args_get_Timestamp( IGattValueChangedEventArgs *iface, DateTime *value )
{
    struct value_changed_args *impl = impl_from_IGattValueChangedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->timestamp;
    return S_OK;
}

static const IGattValueChangedEventArgsVtbl value_changed_args_vtbl =
{
    /* IUnknown */
    value_changed_args_QueryInterface,
    value_changed_args_AddRef,
    value_changed_args_Release,
    /* IInspectable */
    value_changed_args_GetIids,
    value_changed_args_GetRuntimeClassName,
    value_changed_args_GetTrustLevel,
    /* IGattValueChangedEventArgs */
    value_changed_args_get_CharacteristicValue,
    value_changed_args_get_Timestamp,
};

static HRESULT value_changed_args_create( const BYTE *data, UINT32 size, IGattValueChangedEventArgs **out )
{
    struct value_changed_args *impl;
    FILETIME time;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = buffer_create( data, size, &impl->value )))
    {
        free( impl );
        return hr;
    }
    impl->IGattValueChangedEventArgs_iface.lpVtbl = &value_changed_args_vtbl;
    impl->ref = 1;
    GetSystemTimeAsFileTime( &time );
    impl->timestamp.UniversalTime = ((INT64)time.dwHighDateTime << 32) | time.dwLowDateTime;
    *out = &impl->IGattValueChangedEventArgs_iface;
    return S_OK;
}

struct services_result
{
    IGattDeviceServicesResult IGattDeviceServicesResult_iface;
    LONG ref;

    GattCommunicationStatus status;
    IVectorView_GattDeviceService *view;
};

static inline struct services_result *impl_from_IGattDeviceServicesResult( IGattDeviceServicesResult *iface )
{
    return CONTAINING_RECORD( iface, struct services_result, IGattDeviceServicesResult_iface );
}

static HRESULT WINAPI services_result_QueryInterface( IGattDeviceServicesResult *iface, REFIID iid, void **out )
{
    struct services_result *impl = impl_from_IGattDeviceServicesResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattDeviceServicesResult ))
    {
        IGattDeviceServicesResult_AddRef(( *out = &impl->IGattDeviceServicesResult_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI services_result_AddRef( IGattDeviceServicesResult *iface )
{
    struct services_result *impl = impl_from_IGattDeviceServicesResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI services_result_Release( IGattDeviceServicesResult *iface )
{
    struct services_result *impl = impl_from_IGattDeviceServicesResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        IVectorView_GattDeviceService_Release( impl->view );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( services_result, IGattDeviceServicesResult, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattDeviceServicesResult )

static HRESULT WINAPI services_result_get_Status( IGattDeviceServicesResult *iface, GattCommunicationStatus *value )
{
    struct services_result *impl = impl_from_IGattDeviceServicesResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI services_result_get_ProtocolError( IGattDeviceServicesResult *iface, IReference_BYTE **value )
{
    struct services_result *impl = impl_from_IGattDeviceServicesResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return protocol_error_create( impl->status, value );
}

static HRESULT WINAPI services_result_get_Services( IGattDeviceServicesResult *iface, IVectorView_GattDeviceService **value )
{
    struct services_result *impl = impl_from_IGattDeviceServicesResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IVectorView_GattDeviceService_AddRef(( *value = impl->view ));
    return S_OK;
}

static const IGattDeviceServicesResultVtbl services_result_vtbl =
{
    /* IUnknown */
    services_result_QueryInterface,
    services_result_AddRef,
    services_result_Release,
    /* IInspectable */
    services_result_GetIids,
    services_result_GetRuntimeClassName,
    services_result_GetTrustLevel,
    /* IGattDeviceServicesResult */
    services_result_get_Status,
    services_result_get_ProtocolError,
    services_result_get_Services,
};

/* Takes ownership of the view; an empty one is created if there is none. */
HRESULT gatt_services_result_create( GattCommunicationStatus status, IVectorView_GattDeviceService *view, IGattDeviceServicesResult **out )
{
    struct services_result *impl;
    HRESULT hr;

    if (!view && FAILED(hr = empty_view_create( &service_vector_iids, (void **)&view ))) return hr;
    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        IVectorView_GattDeviceService_Release( view );
        return E_OUTOFMEMORY;
    }
    impl->IGattDeviceServicesResult_iface.lpVtbl = &services_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    impl->view = view;
    *out = &impl->IGattDeviceServicesResult_iface;
    return S_OK;
}

struct characteristics_result
{
    IGattCharacteristicsResult IGattCharacteristicsResult_iface;
    LONG ref;

    GattCommunicationStatus status;
    IVectorView_GattCharacteristic *view;
};

static inline struct characteristics_result *impl_from_IGattCharacteristicsResult( IGattCharacteristicsResult *iface )
{
    return CONTAINING_RECORD( iface, struct characteristics_result, IGattCharacteristicsResult_iface );
}

static HRESULT WINAPI characteristics_result_QueryInterface( IGattCharacteristicsResult *iface, REFIID iid, void **out )
{
    struct characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattCharacteristicsResult ))
    {
        IGattCharacteristicsResult_AddRef(( *out = &impl->IGattCharacteristicsResult_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI characteristics_result_AddRef( IGattCharacteristicsResult *iface )
{
    struct characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI characteristics_result_Release( IGattCharacteristicsResult *iface )
{
    struct characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        IVectorView_GattCharacteristic_Release( impl->view );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( characteristics_result, IGattCharacteristicsResult, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattCharacteristicsResult )

static HRESULT WINAPI characteristics_result_get_Status( IGattCharacteristicsResult *iface, GattCommunicationStatus *value )
{
    struct characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI characteristics_result_get_ProtocolError( IGattCharacteristicsResult *iface, IReference_BYTE **value )
{
    struct characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return protocol_error_create( impl->status, value );
}

static HRESULT WINAPI characteristics_result_get_Characteristics( IGattCharacteristicsResult *iface, IVectorView_GattCharacteristic **value )
{
    struct characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IVectorView_GattCharacteristic_AddRef(( *value = impl->view ));
    return S_OK;
}

static const IGattCharacteristicsResultVtbl characteristics_result_vtbl =
{
    /* IUnknown */
    characteristics_result_QueryInterface,
    characteristics_result_AddRef,
    characteristics_result_Release,
    /* IInspectable */
    characteristics_result_GetIids,
    characteristics_result_GetRuntimeClassName,
    characteristics_result_GetTrustLevel,
    /* IGattCharacteristicsResult */
    characteristics_result_get_Status,
    characteristics_result_get_ProtocolError,
    characteristics_result_get_Characteristics,
};

/* Takes ownership of the view; an empty one is created if there is none. */
static HRESULT characteristics_result_create( GattCommunicationStatus status, IVectorView_GattCharacteristic *view, IGattCharacteristicsResult **out )
{
    struct characteristics_result *impl;
    HRESULT hr;

    if (!view && FAILED(hr = empty_view_create( &characteristic_vector_iids, (void **)&view ))) return hr;
    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        IVectorView_GattCharacteristic_Release( view );
        return E_OUTOFMEMORY;
    }
    impl->IGattCharacteristicsResult_iface.lpVtbl = &characteristics_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    impl->view = view;
    *out = &impl->IGattCharacteristicsResult_iface;
    return S_OK;
}

struct descriptors_result
{
    IGattDescriptorsResult IGattDescriptorsResult_iface;
    LONG ref;

    GattCommunicationStatus status;
    IVectorView_GattDescriptor *view;
};

static inline struct descriptors_result *impl_from_IGattDescriptorsResult( IGattDescriptorsResult *iface )
{
    return CONTAINING_RECORD( iface, struct descriptors_result, IGattDescriptorsResult_iface );
}

static HRESULT WINAPI descriptors_result_QueryInterface( IGattDescriptorsResult *iface, REFIID iid, void **out )
{
    struct descriptors_result *impl = impl_from_IGattDescriptorsResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattDescriptorsResult ))
    {
        IGattDescriptorsResult_AddRef(( *out = &impl->IGattDescriptorsResult_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI descriptors_result_AddRef( IGattDescriptorsResult *iface )
{
    struct descriptors_result *impl = impl_from_IGattDescriptorsResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI descriptors_result_Release( IGattDescriptorsResult *iface )
{
    struct descriptors_result *impl = impl_from_IGattDescriptorsResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        IVectorView_GattDescriptor_Release( impl->view );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( descriptors_result, IGattDescriptorsResult, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattDescriptorsResult )

static HRESULT WINAPI descriptors_result_get_Status( IGattDescriptorsResult *iface, GattCommunicationStatus *value )
{
    struct descriptors_result *impl = impl_from_IGattDescriptorsResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI descriptors_result_get_ProtocolError( IGattDescriptorsResult *iface, IReference_BYTE **value )
{
    struct descriptors_result *impl = impl_from_IGattDescriptorsResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return protocol_error_create( impl->status, value );
}

static HRESULT WINAPI descriptors_result_get_Descriptors( IGattDescriptorsResult *iface, IVectorView_GattDescriptor **value )
{
    struct descriptors_result *impl = impl_from_IGattDescriptorsResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IVectorView_GattDescriptor_AddRef(( *value = impl->view ));
    return S_OK;
}

static const IGattDescriptorsResultVtbl descriptors_result_vtbl =
{
    /* IUnknown */
    descriptors_result_QueryInterface,
    descriptors_result_AddRef,
    descriptors_result_Release,
    /* IInspectable */
    descriptors_result_GetIids,
    descriptors_result_GetRuntimeClassName,
    descriptors_result_GetTrustLevel,
    /* IGattDescriptorsResult */
    descriptors_result_get_Status,
    descriptors_result_get_ProtocolError,
    descriptors_result_get_Descriptors,
};

/* Takes ownership of the view; an empty one is created if there is none. */
static HRESULT descriptors_result_create( GattCommunicationStatus status, IVectorView_GattDescriptor *view, IGattDescriptorsResult **out )
{
    struct descriptors_result *impl;
    HRESULT hr;

    if (!view && FAILED(hr = empty_view_create( &descriptor_vector_iids, (void **)&view ))) return hr;
    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        IVectorView_GattDescriptor_Release( view );
        return E_OUTOFMEMORY;
    }
    impl->IGattDescriptorsResult_iface.lpVtbl = &descriptors_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    impl->view = view;
    *out = &impl->IGattDescriptorsResult_iface;
    return S_OK;
}

struct read_result
{
    IGattReadResult IGattReadResult_iface;
    LONG ref;

    GattCommunicationStatus status;
    IBuffer *value;
};

static inline struct read_result *impl_from_IGattReadResult( IGattReadResult *iface )
{
    return CONTAINING_RECORD( iface, struct read_result, IGattReadResult_iface );
}

static HRESULT WINAPI read_result_QueryInterface( IGattReadResult *iface, REFIID iid, void **out )
{
    struct read_result *impl = impl_from_IGattReadResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattReadResult ))
    {
        IGattReadResult_AddRef(( *out = &impl->IGattReadResult_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI read_result_AddRef( IGattReadResult *iface )
{
    struct read_result *impl = impl_from_IGattReadResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI read_result_Release( IGattReadResult *iface )
{
    struct read_result *impl = impl_from_IGattReadResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        if (impl->value) IBuffer_Release( impl->value );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( read_result, IGattReadResult, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattReadResult )

static HRESULT WINAPI read_result_get_Status( IGattReadResult *iface, GattCommunicationStatus *value )
{
    struct read_result *impl = impl_from_IGattReadResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI read_result_get_Value( IGattReadResult *iface, IBuffer **value )
{
    struct read_result *impl = impl_from_IGattReadResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->value)) IBuffer_AddRef( *value );
    return S_OK;
}

static const IGattReadResultVtbl read_result_vtbl =
{
    /* IUnknown */
    read_result_QueryInterface,
    read_result_AddRef,
    read_result_Release,
    /* IInspectable */
    read_result_GetIids,
    read_result_GetRuntimeClassName,
    read_result_GetTrustLevel,
    /* IGattReadResult */
    read_result_get_Status,
    read_result_get_Value,
};

/* Takes ownership of the value. */
static HRESULT read_result_create( GattCommunicationStatus status, IBuffer *value, IGattReadResult **out )
{
    struct read_result *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        if (value) IBuffer_Release( value );
        return E_OUTOFMEMORY;
    }
    impl->IGattReadResult_iface.lpVtbl = &read_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    impl->value = value;
    *out = &impl->IGattReadResult_iface;
    return S_OK;
}

struct write_result
{
    IGattWriteResult IGattWriteResult_iface;
    LONG ref;

    GattCommunicationStatus status;
};

static inline struct write_result *impl_from_IGattWriteResult( IGattWriteResult *iface )
{
    return CONTAINING_RECORD( iface, struct write_result, IGattWriteResult_iface );
}

static HRESULT WINAPI write_result_QueryInterface( IGattWriteResult *iface, REFIID iid, void **out )
{
    struct write_result *impl = impl_from_IGattWriteResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattWriteResult ))
    {
        IGattWriteResult_AddRef(( *out = &impl->IGattWriteResult_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI write_result_AddRef( IGattWriteResult *iface )
{
    struct write_result *impl = impl_from_IGattWriteResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI write_result_Release( IGattWriteResult *iface )
{
    struct write_result *impl = impl_from_IGattWriteResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( write_result, IGattWriteResult, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattWriteResult )

static HRESULT WINAPI write_result_get_Status( IGattWriteResult *iface, GattCommunicationStatus *value )
{
    struct write_result *impl = impl_from_IGattWriteResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI write_result_get_ProtocolError( IGattWriteResult *iface, IReference_BYTE **value )
{
    struct write_result *impl = impl_from_IGattWriteResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return protocol_error_create( impl->status, value );
}

static const IGattWriteResultVtbl write_result_vtbl =
{
    /* IUnknown */
    write_result_QueryInterface,
    write_result_AddRef,
    write_result_Release,
    /* IInspectable */
    write_result_GetIids,
    write_result_GetRuntimeClassName,
    write_result_GetTrustLevel,
    /* IGattWriteResult */
    write_result_get_Status,
    write_result_get_ProtocolError,
};

static HRESULT write_result_create( GattCommunicationStatus status, IGattWriteResult **out )
{
    struct write_result *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IGattWriteResult_iface.lpVtbl = &write_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    *out = &impl->IGattWriteResult_iface;
    return S_OK;
}

struct cccd_result
{
    IGattReadClientCharacteristicConfigurationDescriptorResult IGattReadClientCharacteristicConfigurationDescriptorResult_iface;
    LONG ref;

    GattCommunicationStatus status;
    GattClientCharacteristicConfigurationDescriptorValue value;
};

static inline struct cccd_result *impl_from_IGattReadClientCharacteristicConfigurationDescriptorResult( IGattReadClientCharacteristicConfigurationDescriptorResult *iface )
{
    return CONTAINING_RECORD( iface, struct cccd_result, IGattReadClientCharacteristicConfigurationDescriptorResult_iface );
}

static HRESULT WINAPI cccd_result_QueryInterface( IGattReadClientCharacteristicConfigurationDescriptorResult *iface, REFIID iid, void **out )
{
    struct cccd_result *impl = impl_from_IGattReadClientCharacteristicConfigurationDescriptorResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattReadClientCharacteristicConfigurationDescriptorResult ))
    {
        IGattReadClientCharacteristicConfigurationDescriptorResult_AddRef(( *out = &impl->IGattReadClientCharacteristicConfigurationDescriptorResult_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI cccd_result_AddRef( IGattReadClientCharacteristicConfigurationDescriptorResult *iface )
{
    struct cccd_result *impl = impl_from_IGattReadClientCharacteristicConfigurationDescriptorResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI cccd_result_Release( IGattReadClientCharacteristicConfigurationDescriptorResult *iface )
{
    struct cccd_result *impl = impl_from_IGattReadClientCharacteristicConfigurationDescriptorResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( cccd_result, IGattReadClientCharacteristicConfigurationDescriptorResult, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattReadClientCharacteristicConfigurationDescriptorResult )

static HRESULT WINAPI cccd_result_get_Status( IGattReadClientCharacteristicConfigurationDescriptorResult *iface, GattCommunicationStatus *value )
{
    struct cccd_result *impl = impl_from_IGattReadClientCharacteristicConfigurationDescriptorResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI cccd_result_get_ClientCharacteristicConfigurationDescriptor(
    IGattReadClientCharacteristicConfigurationDescriptorResult *iface,
    GattClientCharacteristicConfigurationDescriptorValue *value )
{
    struct cccd_result *impl = impl_from_IGattReadClientCharacteristicConfigurationDescriptorResult( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->value;
    return S_OK;
}

static const IGattReadClientCharacteristicConfigurationDescriptorResultVtbl cccd_result_vtbl =
{
    /* IUnknown */
    cccd_result_QueryInterface,
    cccd_result_AddRef,
    cccd_result_Release,
    /* IInspectable */
    cccd_result_GetIids,
    cccd_result_GetRuntimeClassName,
    cccd_result_GetTrustLevel,
    /* IGattReadClientCharacteristicConfigurationDescriptorResult */
    cccd_result_get_Status,
    cccd_result_get_ClientCharacteristicConfigurationDescriptor,
};

static HRESULT cccd_result_create( GattClientCharacteristicConfigurationDescriptorValue value,
                                   IGattReadClientCharacteristicConfigurationDescriptorResult **out )
{
    struct cccd_result *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IGattReadClientCharacteristicConfigurationDescriptorResult_iface.lpVtbl = &cccd_result_vtbl;
    impl->ref = 1;
    impl->status = GattCommunicationStatus_Success;
    impl->value = value;
    *out = &impl->IGattReadClientCharacteristicConfigurationDescriptorResult_iface;
    return S_OK;
}

struct gatt_characteristic
{
    IGattCharacteristic IGattCharacteristic_iface;
    IGattCharacteristic2 IGattCharacteristic2_iface;
    IGattCharacteristic3 IGattCharacteristic3_iface;
    LONG ref;

    struct list entry; /* In ble_device.characteristics. */
    struct ble_device *device;
    IGattDeviceService *service;
    BTH_LE_GATT_CHARACTERISTIC props;
    /* BlueZ doesn't expose the descriptor, so remember what was last written to it. */
    GattClientCharacteristicConfigurationDescriptorValue cccd;
    struct list value_handlers;
};

static inline struct gatt_characteristic *impl_from_IGattCharacteristic( IGattCharacteristic *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_characteristic, IGattCharacteristic_iface );
}

static HRESULT WINAPI gatt_characteristic_QueryInterface( IGattCharacteristic *iface, REFIID iid, void **out )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattCharacteristic ))
    {
        IGattCharacteristic_AddRef(( *out = &impl->IGattCharacteristic_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IGattCharacteristic2 ))
    {
        IGattCharacteristic2_AddRef(( *out = &impl->IGattCharacteristic2_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IGattCharacteristic3 ))
    {
        IGattCharacteristic3_AddRef(( *out = &impl->IGattCharacteristic3_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_characteristic_AddRef( IGattCharacteristic *iface )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI gatt_characteristic_Release( IGattCharacteristic *iface )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        EnterCriticalSection( &impl->device->cs );
        list_remove( &impl->entry );
        LeaveCriticalSection( &impl->device->cs );
        event_handlers_clear( &impl->value_handlers );
        IGattDeviceService_Release( impl->service );
        IBluetoothLEDevice_Release( &impl->device->IBluetoothLEDevice_iface );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( gatt_characteristic, IGattCharacteristic, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattCharacteristic )

static HRESULT gatt_characteristic_get_descriptors( struct gatt_characteristic *impl, const GUID *uuid,
                                                    IVectorView_GattDescriptor **view );

static HRESULT WINAPI gatt_characteristic_GetDescriptors( IGattCharacteristic *iface, GUID uuid, IVectorView_GattDescriptor **value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), value );

    if (!value) return E_POINTER;
    return gatt_characteristic_get_descriptors( impl, &uuid, value );
}

static HRESULT WINAPI gatt_characteristic_get_CharacteristicProperties( IGattCharacteristic *iface, GattCharacteristicProperties *value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    const BTH_LE_GATT_CHARACTERISTIC *props = &impl->props;

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = GattCharacteristicProperties_None;
    if (props->IsBroadcastable) *value |= GattCharacteristicProperties_Broadcast;
    if (props->IsReadable) *value |= GattCharacteristicProperties_Read;
    if (props->IsWritableWithoutResponse) *value |= GattCharacteristicProperties_WriteWithoutResponse;
    if (props->IsWritable) *value |= GattCharacteristicProperties_Write;
    if (props->IsNotifiable) *value |= GattCharacteristicProperties_Notify;
    if (props->IsIndicatable) *value |= GattCharacteristicProperties_Indicate;
    if (props->IsSignedWritable) *value |= GattCharacteristicProperties_AuthenticatedSignedWrites;
    if (props->HasExtendedProperties) *value |= GattCharacteristicProperties_ExtendedProperties;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_ProtectionLevel( IGattCharacteristic *iface, GattProtectionLevel *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = GattProtectionLevel_Plain;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_put_ProtectionLevel( IGattCharacteristic *iface, GattProtectionLevel value )
{
    FIXME( "(%p, %d): stub!\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_UserDescription( IGattCharacteristic *iface, HSTRING *value )
{
    FIXME( "(%p, %p): semi-stub!\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_Uuid( IGattCharacteristic *iface, GUID *value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    le_uuid_to_guid( &impl->props.CharacteristicUuid, value );
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_AttributeHandle( IGattCharacteristic *iface, UINT16 *value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->props.AttributeHandle;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_PresentationFormats( IGattCharacteristic *iface, IVectorView_GattPresentationFormat **value )
{
    FIXME( "(%p, %p): semi-stub!\n", iface, value );

    if (!value) return E_POINTER;
    return empty_view_create( &presentation_format_vector_iids, (void **)value );
}

static HRESULT gatt_characteristic_read_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    BYTE buffer[offsetof( struct winebth_le_device_characteristic_value_params, buf[512] )] = {0};
    struct winebth_le_device_characteristic_value_params *value = (void *)buffer;
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );
    GattCommunicationStatus status = GattCommunicationStatus_Success;
    struct async_params *params = impl_from_IUnknown( param );
    IGattReadResult *read_result;
    IBuffer *data = NULL;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;

    value->handle = impl->props.AttributeHandle;
    value->from_device = params->mode != BluetoothCacheMode_Cached;
    if (!ble_device_ioctl( impl->device, IOCTL_WINEBTH_LE_DEVICE_READ_CHARACTERISTIC, value, sizeof( *value ),
                           value, sizeof( buffer ), NULL ))
        status = gatt_status_from_error( impl->device );
    else if (FAILED(hr = buffer_create( value->buf, value->size, &data )))
        return hr;

    if (FAILED(hr = read_result_create( status, data, &read_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)read_result;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_ReadValueWithCacheModeAsync( IGattCharacteristic *iface, BluetoothCacheMode mode,
                                                                       IAsyncOperation_GattReadResult **value )
{
    TRACE( "(%p, %d, %p)\n", iface, mode, value );

    if (!value) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattReadResult, iface, async_params_create( NULL, mode ),
                                  gatt_characteristic_read_async, FALSE, value );
}

static HRESULT WINAPI gatt_characteristic_ReadValueAsync( IGattCharacteristic *iface, IAsyncOperation_GattReadResult **value )
{
    /* The values that BlueZ caches can be arbitrarily old, so only use them when asked to. */
    return gatt_characteristic_ReadValueWithCacheModeAsync( iface, BluetoothCacheMode_Uncached, value );
}

static GattCommunicationStatus gatt_characteristic_write( struct gatt_characteristic *impl, struct async_params *params )
{
    struct winebth_le_device_characteristic_value_params *value;
    GattCommunicationStatus status = GattCommunicationStatus_Success;
    UINT32 size, total;
    BYTE *data;

    if (FAILED(buffer_get_bytes( params->buffer, &data, &size ))) return GattCommunicationStatus_ProtocolError;
    total = offsetof( struct winebth_le_device_characteristic_value_params, buf[size] );
    if (!(value = calloc( 1, max( total, sizeof( *value ) ) ))) return GattCommunicationStatus_Unreachable;
    value->handle = impl->props.AttributeHandle;
    value->without_response = params->option == GattWriteOption_WriteWithoutResponse;
    value->size = size;
    memcpy( value->buf, data, size );
    if (!ble_device_ioctl( impl->device, IOCTL_WINEBTH_LE_DEVICE_WRITE_CHARACTERISTIC, value,
                           max( total, sizeof( *value ) ), NULL, 0, NULL ))
        status = gatt_status_from_error( impl->device );
    free( value );
    return status;
}

static GattCommunicationStatus gatt_characteristic_write_cccd( struct gatt_characteristic *impl, struct async_params *params )
{
    struct winebth_le_device_set_notify_params notify = {0};

    notify.handle = impl->props.AttributeHandle;
    notify.enable = params->cccd != GattClientCharacteristicConfigurationDescriptorValue_None;
    if (!ble_device_ioctl( impl->device, IOCTL_WINEBTH_LE_DEVICE_SET_NOTIFY, &notify, sizeof( notify ), NULL, 0, NULL ))
        return gatt_status_from_error( impl->device );
    impl->cccd = params->cccd;
    return GattCommunicationStatus_Success;
}

static HRESULT gatt_characteristic_write_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );

    if (!called_async) return STATUS_PENDING;
    result->vt = VT_I4;
    result->lVal = gatt_characteristic_write( impl, impl_from_IUnknown( param ) );
    return S_OK;
}

static HRESULT gatt_characteristic_write_cccd_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );

    if (!called_async) return STATUS_PENDING;
    result->vt = VT_I4;
    result->lVal = gatt_characteristic_write_cccd( impl, impl_from_IUnknown( param ) );
    return S_OK;
}

static HRESULT gatt_characteristic_write_result_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );
    struct async_params *params = impl_from_IUnknown( param );
    IGattWriteResult *write_result;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    /* Writes of the client configuration descriptor don't have a buffer. */
    hr = write_result_create( params->buffer ? gatt_characteristic_write( impl, params )
                                             : gatt_characteristic_write_cccd( impl, params ), &write_result );
    if (FAILED(hr)) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)write_result;
    return S_OK;
}

static struct async_params *write_params_create( IBuffer *buffer, GattWriteOption option )
{
    struct async_params *params;

    if (!(params = async_params_create( NULL, BluetoothCacheMode_Uncached ))) return NULL;
    IBuffer_AddRef(( params->buffer = buffer ));
    params->option = option;
    return params;
}

static struct async_params *cccd_params_create( GattClientCharacteristicConfigurationDescriptorValue value )
{
    struct async_params *params;

    if (!(params = async_params_create( NULL, BluetoothCacheMode_Uncached ))) return NULL;
    params->cccd = value;
    return params;
}

static HRESULT WINAPI gatt_characteristic_WriteValueWithOptionAsync( IGattCharacteristic *iface, IBuffer *value, GattWriteOption option,
                                                                     IAsyncOperation_GattCommunicationStatus **async )
{
    TRACE( "(%p, %p, %d, %p)\n", iface, value, option, async );

    if (!value || !async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattCommunicationStatus, iface, write_params_create( value, option ),
                                  gatt_characteristic_write_async, TRUE, async );
}

static HRESULT WINAPI gatt_characteristic_WriteValueAsync( IGattCharacteristic *iface, IBuffer *value,
                                                           IAsyncOperation_GattCommunicationStatus **async )
{
    return gatt_characteristic_WriteValueWithOptionAsync( iface, value, GattWriteOption_WriteWithResponse, async );
}

static HRESULT gatt_characteristic_read_cccd_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );
    IGattReadClientCharacteristicConfigurationDescriptorResult *cccd_result;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (FAILED(hr = cccd_result_create( impl->cccd, &cccd_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)cccd_result;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_ReadClientCharacteristicConfigurationDescriptorAsync(
    IGattCharacteristic *iface, IAsyncOperation_GattReadClientCharacteristicConfigurationDescriptorResult **async )
{
    TRACE( "(%p, %p)\n", iface, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattReadClientCharacteristicConfigurationDescriptorResult, iface,
                                  async_params_create( NULL, BluetoothCacheMode_Uncached ),
                                  gatt_characteristic_read_cccd_async, FALSE, async );
}

static HRESULT WINAPI gatt_characteristic_WriteClientCharacteristicConfigurationDescriptorAsync(
    IGattCharacteristic *iface, GattClientCharacteristicConfigurationDescriptorValue value,
    IAsyncOperation_GattCommunicationStatus **async )
{
    TRACE( "(%p, %d, %p)\n", iface, value, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattCommunicationStatus, iface, cccd_params_create( value ),
                                  gatt_characteristic_write_cccd_async, TRUE, async );
}

static HRESULT WINAPI gatt_characteristic_add_ValueChanged( IGattCharacteristic *iface,
                                                            ITypedEventHandler_GattCharacteristic_GattValueChangedEventArgs *handler,
                                                            EventRegistrationToken *token )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    if (!token) return E_POINTER;
    return event_handlers_append( &impl->value_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI gatt_characteristic_remove_ValueChanged( IGattCharacteristic *iface, EventRegistrationToken token )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %I64x)\n", iface, token.value );

    return event_handlers_remove( &impl->value_handlers, &token );
}

/* The client characteristic configuration descriptor of a characteristic. BlueZ doesn't expose it and
 * manages its value itself, so reads and writes of it are emulated. */
struct gatt_descriptor
{
    IGattDescriptor IGattDescriptor_iface;
    IGattDescriptor2 IGattDescriptor2_iface;
    LONG ref;

    struct gatt_characteristic *characteristic;
};

static const GUID cccd_uuid = { 0x2902, 0, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };

static inline struct gatt_descriptor *impl_from_IGattDescriptor( IGattDescriptor *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_descriptor, IGattDescriptor_iface );
}

static HRESULT WINAPI gatt_descriptor_QueryInterface( IGattDescriptor *iface, REFIID iid, void **out )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattDescriptor ))
    {
        IGattDescriptor_AddRef(( *out = &impl->IGattDescriptor_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IGattDescriptor2 ))
    {
        IGattDescriptor2_AddRef(( *out = &impl->IGattDescriptor2_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_descriptor_AddRef( IGattDescriptor *iface )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI gatt_descriptor_Release( IGattDescriptor *iface )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        IGattCharacteristic_Release( &impl->characteristic->IGattCharacteristic_iface );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( gatt_descriptor, IGattDescriptor, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattDescriptor )

static HRESULT WINAPI gatt_descriptor_get_ProtectionLevel( IGattDescriptor *iface, GattProtectionLevel *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = GattProtectionLevel_Plain;
    return S_OK;
}

static HRESULT WINAPI gatt_descriptor_put_ProtectionLevel( IGattDescriptor *iface, GattProtectionLevel value )
{
    FIXME( "(%p, %d): stub!\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI gatt_descriptor_get_Uuid( IGattDescriptor *iface, GUID *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = cccd_uuid;
    return S_OK;
}

static HRESULT WINAPI gatt_descriptor_get_AttributeHandle( IGattDescriptor *iface, UINT16 *value )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    /* The descriptor normally follows the declaration and the value of its characteristic. */
    *value = impl->characteristic->props.AttributeHandle + 2;
    return S_OK;
}

static HRESULT gatt_descriptor_read_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( (IGattDescriptor *)invoker );
    IGattReadResult *read_result;
    IBuffer *data;
    BYTE value[2];
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;

    value[0] = impl->characteristic->cccd;
    value[1] = 0;
    if (FAILED(hr = buffer_create( value, sizeof( value ), &data ))) return hr;
    if (FAILED(hr = read_result_create( GattCommunicationStatus_Success, data, &read_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)read_result;
    return S_OK;
}

static HRESULT WINAPI gatt_descriptor_ReadValueWithCacheModeAsync( IGattDescriptor *iface, BluetoothCacheMode mode,
                                                                   IAsyncOperation_GattReadResult **async )
{
    TRACE( "(%p, %d, %p)\n", iface, mode, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattReadResult, iface, async_params_create( NULL, mode ),
                                  gatt_descriptor_read_async, FALSE, async );
}

static HRESULT WINAPI gatt_descriptor_ReadValueAsync( IGattDescriptor *iface, IAsyncOperation_GattReadResult **async )
{
    return gatt_descriptor_ReadValueWithCacheModeAsync( iface, BluetoothCacheMode_Cached, async );
}

static GattCommunicationStatus gatt_descriptor_write( struct gatt_descriptor *impl, struct async_params *params )
{
    UINT32 size;
    BYTE *data;

    if (FAILED(buffer_get_bytes( params->buffer, &data, &size )) || !size) return GattCommunicationStatus_ProtocolError;
    params->cccd = data[0] & 3;
    return gatt_characteristic_write_cccd( impl->characteristic, params );
}

static HRESULT gatt_descriptor_write_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( (IGattDescriptor *)invoker );

    if (!called_async) return STATUS_PENDING;
    result->vt = VT_I4;
    result->lVal = gatt_descriptor_write( impl, impl_from_IUnknown( param ) );
    return S_OK;
}

static HRESULT gatt_descriptor_write_result_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor( (IGattDescriptor *)invoker );
    IGattWriteResult *write_result;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (FAILED(hr = write_result_create( gatt_descriptor_write( impl, impl_from_IUnknown( param ) ), &write_result )))
        return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)write_result;
    return S_OK;
}

static HRESULT WINAPI gatt_descriptor_WriteValueAsync( IGattDescriptor *iface, IBuffer *value,
                                                       IAsyncOperation_GattCommunicationStatus **async )
{
    TRACE( "(%p, %p, %p)\n", iface, value, async );

    if (!value || !async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattCommunicationStatus, iface,
                                  write_params_create( value, GattWriteOption_WriteWithResponse ),
                                  gatt_descriptor_write_async, TRUE, async );
}

static const IGattDescriptorVtbl gatt_descriptor_vtbl =
{
    /* IUnknown */
    gatt_descriptor_QueryInterface,
    gatt_descriptor_AddRef,
    gatt_descriptor_Release,
    /* IInspectable */
    gatt_descriptor_GetIids,
    gatt_descriptor_GetRuntimeClassName,
    gatt_descriptor_GetTrustLevel,
    /* IGattDescriptor */
    gatt_descriptor_get_ProtectionLevel,
    gatt_descriptor_put_ProtectionLevel,
    gatt_descriptor_get_Uuid,
    gatt_descriptor_get_AttributeHandle,
    gatt_descriptor_ReadValueAsync,
    gatt_descriptor_ReadValueWithCacheModeAsync,
    gatt_descriptor_WriteValueAsync,
};

DEFINE_IINSPECTABLE( gatt_descriptor2, IGattDescriptor2, struct gatt_descriptor, IGattDescriptor_iface )

static HRESULT WINAPI gatt_descriptor2_WriteValueWithResultAsync( IGattDescriptor2 *iface, IBuffer *value,
                                                                  IAsyncOperation_GattWriteResult **async )
{
    struct gatt_descriptor *impl = impl_from_IGattDescriptor2( iface );

    TRACE( "(%p, %p, %p)\n", iface, value, async );

    if (!value || !async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattWriteResult, &impl->IGattDescriptor_iface,
                                  write_params_create( value, GattWriteOption_WriteWithResponse ),
                                  gatt_descriptor_write_result_async, FALSE, async );
}

static const IGattDescriptor2Vtbl gatt_descriptor2_vtbl =
{
    /* IUnknown */
    gatt_descriptor2_QueryInterface,
    gatt_descriptor2_AddRef,
    gatt_descriptor2_Release,
    /* IInspectable */
    gatt_descriptor2_GetIids,
    gatt_descriptor2_GetRuntimeClassName,
    gatt_descriptor2_GetTrustLevel,
    /* IGattDescriptor2 */
    gatt_descriptor2_WriteValueWithResultAsync,
};

/* Get the descriptors of the characteristic, optionally only those with the given UUID. */
static HRESULT gatt_characteristic_get_descriptors( struct gatt_characteristic *impl, const GUID *uuid,
                                                    IVectorView_GattDescriptor **view )
{
    IVector_IInspectable *vector;
    struct gatt_descriptor *desc;
    HRESULT hr;

    /* TODO: BlueZ exposes the other descriptors as GattDescriptor1 objects. */
    if (FAILED(hr = vector_create( &descriptor_vector_iids, (void **)&vector ))) return hr;
    if ((impl->props.IsNotifiable || impl->props.IsIndicatable) && (!uuid || IsEqualGUID( uuid, &cccd_uuid )))
    {
        if (!(desc = calloc( 1, sizeof( *desc ) )))
        {
            IVector_IInspectable_Release( vector );
            return E_OUTOFMEMORY;
        }
        desc->IGattDescriptor_iface.lpVtbl = &gatt_descriptor_vtbl;
        desc->IGattDescriptor2_iface.lpVtbl = &gatt_descriptor2_vtbl;
        desc->ref = 1;
        IGattCharacteristic_AddRef( &(desc->characteristic = impl)->IGattCharacteristic_iface );
        hr = IVector_IInspectable_Append( vector, (IInspectable *)&desc->IGattDescriptor_iface );
        IGattDescriptor_Release( &desc->IGattDescriptor_iface );
    }
    if (SUCCEEDED(hr)) hr = IVector_IInspectable_GetView( vector, (IVectorView_IInspectable **)view );
    IVector_IInspectable_Release( vector );
    return hr;
}

static const IGattCharacteristicVtbl gatt_characteristic_vtbl =
{
    /* IUnknown */
    gatt_characteristic_QueryInterface,
    gatt_characteristic_AddRef,
    gatt_characteristic_Release,
    /* IInspectable */
    gatt_characteristic_GetIids,
    gatt_characteristic_GetRuntimeClassName,
    gatt_characteristic_GetTrustLevel,
    /* IGattCharacteristic */
    gatt_characteristic_GetDescriptors,
    gatt_characteristic_get_CharacteristicProperties,
    gatt_characteristic_get_ProtectionLevel,
    gatt_characteristic_put_ProtectionLevel,
    gatt_characteristic_get_UserDescription,
    gatt_characteristic_get_Uuid,
    gatt_characteristic_get_AttributeHandle,
    gatt_characteristic_get_PresentationFormats,
    gatt_characteristic_ReadValueAsync,
    gatt_characteristic_ReadValueWithCacheModeAsync,
    gatt_characteristic_WriteValueAsync,
    gatt_characteristic_WriteValueWithOptionAsync,
    gatt_characteristic_ReadClientCharacteristicConfigurationDescriptorAsync,
    gatt_characteristic_WriteClientCharacteristicConfigurationDescriptorAsync,
    gatt_characteristic_add_ValueChanged,
    gatt_characteristic_remove_ValueChanged,
};

DEFINE_IINSPECTABLE( gatt_characteristic2, IGattCharacteristic2, struct gatt_characteristic, IGattCharacteristic_iface )

static HRESULT WINAPI gatt_characteristic2_get_Service( IGattCharacteristic2 *iface, IGattDeviceService **value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic2( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    IGattDeviceService_AddRef(( *value = impl->service ));
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic2_GetAllDescriptors( IGattCharacteristic2 *iface, IVectorView_GattDescriptor **descriptors )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic2( iface );

    TRACE( "(%p, %p)\n", iface, descriptors );

    if (!descriptors) return E_POINTER;
    return gatt_characteristic_get_descriptors( impl, NULL, descriptors );
}

static const IGattCharacteristic2Vtbl gatt_characteristic2_vtbl =
{
    /* IUnknown */
    gatt_characteristic2_QueryInterface,
    gatt_characteristic2_AddRef,
    gatt_characteristic2_Release,
    /* IInspectable */
    gatt_characteristic2_GetIids,
    gatt_characteristic2_GetRuntimeClassName,
    gatt_characteristic2_GetTrustLevel,
    /* IGattCharacteristic2 */
    gatt_characteristic2_get_Service,
    gatt_characteristic2_GetAllDescriptors,
};

DEFINE_IINSPECTABLE( gatt_characteristic3, IGattCharacteristic3, struct gatt_characteristic, IGattCharacteristic_iface )

static HRESULT gatt_characteristic_get_descriptors_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );
    GattCommunicationStatus status = GattCommunicationStatus_Success;
    struct async_params *params = impl_from_IUnknown( param );
    IGattDescriptorsResult *descriptors_result;
    IVectorView_GattDescriptor *view = NULL;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (FAILED(gatt_characteristic_get_descriptors( impl, params->has_uuid ? &params->uuid : NULL, &view )))
        status = GattCommunicationStatus_Unreachable;
    if (FAILED(hr = descriptors_result_create( status, view, &descriptors_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)descriptors_result;
    return S_OK;
}

static HRESULT gatt_characteristic_get_descriptors_start( IGattCharacteristic3 *iface, const GUID *uuid, BluetoothCacheMode mode,
                                                          IAsyncOperation_GattDescriptorsResult **async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic3( iface );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattDescriptorsResult, &impl->IGattCharacteristic_iface,
                                  async_params_create( uuid, mode ), gatt_characteristic_get_descriptors_async, FALSE, async );
}

static HRESULT WINAPI gatt_characteristic3_GetDescriptorsAsync( IGattCharacteristic3 *iface, IAsyncOperation_GattDescriptorsResult **async )
{
    TRACE( "(%p, %p)\n", iface, async );
    return gatt_characteristic_get_descriptors_start( iface, NULL, BluetoothCacheMode_Cached, async );
}

static HRESULT WINAPI gatt_characteristic3_GetDescriptorsWithCacheModeAsync( IGattCharacteristic3 *iface, BluetoothCacheMode mode,
                                                                             IAsyncOperation_GattDescriptorsResult **async )
{
    TRACE( "(%p, %d, %p)\n", iface, mode, async );
    return gatt_characteristic_get_descriptors_start( iface, NULL, mode, async );
}

static HRESULT WINAPI gatt_characteristic3_GetDescriptorsForUuidAsync( IGattCharacteristic3 *iface, GUID uuid,
                                                                       IAsyncOperation_GattDescriptorsResult **async )
{
    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), async );
    return gatt_characteristic_get_descriptors_start( iface, &uuid, BluetoothCacheMode_Cached, async );
}

static HRESULT WINAPI gatt_characteristic3_GetDescriptorsForUuidWithCacheModeAsync( IGattCharacteristic3 *iface, GUID uuid,
                                                                                    BluetoothCacheMode mode,
                                                                                    IAsyncOperation_GattDescriptorsResult **async )
{
    TRACE( "(%p, %s, %d, %p)\n", iface, debugstr_guid( &uuid ), mode, async );
    return gatt_characteristic_get_descriptors_start( iface, &uuid, mode, async );
}

static HRESULT WINAPI gatt_characteristic3_WriteValueWithResultAndOptionAsync( IGattCharacteristic3 *iface, IBuffer *value,
                                                                               GattWriteOption option,
                                                                               IAsyncOperation_GattWriteResult **async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic3( iface );

    TRACE( "(%p, %p, %d, %p)\n", iface, value, option, async );

    if (!value || !async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattWriteResult, &impl->IGattCharacteristic_iface,
                                  write_params_create( value, option ), gatt_characteristic_write_result_async, FALSE, async );
}

static HRESULT WINAPI gatt_characteristic3_WriteValueWithResultAsync( IGattCharacteristic3 *iface, IBuffer *value,
                                                                      IAsyncOperation_GattWriteResult **async )
{
    return gatt_characteristic3_WriteValueWithResultAndOptionAsync( iface, value, GattWriteOption_WriteWithResponse, async );
}

static HRESULT WINAPI gatt_characteristic3_WriteClientCharacteristicConfigurationDescriptorWithResultAsync(
    IGattCharacteristic3 *iface, GattClientCharacteristicConfigurationDescriptorValue value,
    IAsyncOperation_GattWriteResult **async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic3( iface );

    TRACE( "(%p, %d, %p)\n", iface, value, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattWriteResult, &impl->IGattCharacteristic_iface,
                                  cccd_params_create( value ), gatt_characteristic_write_result_async, FALSE, async );
}

static const IGattCharacteristic3Vtbl gatt_characteristic3_vtbl =
{
    /* IUnknown */
    gatt_characteristic3_QueryInterface,
    gatt_characteristic3_AddRef,
    gatt_characteristic3_Release,
    /* IInspectable */
    gatt_characteristic3_GetIids,
    gatt_characteristic3_GetRuntimeClassName,
    gatt_characteristic3_GetTrustLevel,
    /* IGattCharacteristic3 */
    gatt_characteristic3_GetDescriptorsAsync,
    gatt_characteristic3_GetDescriptorsWithCacheModeAsync,
    gatt_characteristic3_GetDescriptorsForUuidAsync,
    gatt_characteristic3_GetDescriptorsForUuidWithCacheModeAsync,
    gatt_characteristic3_WriteValueWithResultAsync,
    gatt_characteristic3_WriteValueWithResultAndOptionAsync,
    gatt_characteristic3_WriteClientCharacteristicConfigurationDescriptorWithResultAsync,
};

static HRESULT gatt_characteristic_create( struct ble_device *device, IGattDeviceService *service,
                                           const BTH_LE_GATT_CHARACTERISTIC *props, IGattCharacteristic **out )
{
    struct gatt_characteristic *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IGattCharacteristic_iface.lpVtbl = &gatt_characteristic_vtbl;
    impl->IGattCharacteristic2_iface.lpVtbl = &gatt_characteristic2_vtbl;
    impl->IGattCharacteristic3_iface.lpVtbl = &gatt_characteristic3_vtbl;
    impl->ref = 1;
    impl->props = *props;
    list_init( &impl->value_handlers );
    IGattDeviceService_AddRef(( impl->service = service ));
    IBluetoothLEDevice_AddRef( &(impl->device = device)->IBluetoothLEDevice_iface );
    EnterCriticalSection( &device->cs );
    list_add_tail( &device->characteristics, &impl->entry );
    LeaveCriticalSection( &device->cs );
    *out = &impl->IGattCharacteristic_iface;
    return S_OK;
}

/* Take a reference, unless the characteristic is being destroyed. */
static BOOL gatt_characteristic_try_addref( struct gatt_characteristic *impl )
{
    LONG ref;

    do
    {
        if (!(ref = impl->ref)) return FALSE;
    } while (InterlockedCompareExchange( &impl->ref, ref + 1, ref ) != ref);
    return TRUE;
}

/* Notify the handlers for the characteristics with this attribute handle of its new value. */
void gatt_characteristics_value_changed( struct ble_device *device, UINT16 handle, const BYTE *data, UINT32 size )
{
    struct gatt_characteristic *impl, *matches[16];
    IGattValueChangedEventArgs *args;
    UINT32 count = 0, i;

    EnterCriticalSection( &device->cs );
    LIST_FOR_EACH_ENTRY( impl, &device->characteristics, struct gatt_characteristic, entry )
    {
        if (impl->props.AttributeHandle != handle || count == ARRAY_SIZE( matches )) continue;
        if (gatt_characteristic_try_addref( impl )) matches[count++] = impl;
    }
    LeaveCriticalSection( &device->cs );

    if (!count) return;
    if (FAILED(value_changed_args_create( data, size, &args ))) args = NULL;
    for (i = 0; i < count; i++)
    {
        if (args)
            event_handlers_notify( &matches[i]->value_handlers, (IInspectable *)&matches[i]->IGattCharacteristic_iface,
                                   (IInspectable *)args );
        IGattCharacteristic_Release( &matches[i]->IGattCharacteristic_iface );
    }
    if (args) IGattValueChangedEventArgs_Release( args );
}

struct gatt_service
{
    IGattDeviceService IGattDeviceService_iface;
    IGattDeviceService3 IGattDeviceService3_iface;
    IClosable IClosable_iface;
    LONG ref;

    struct ble_device *device;
    BTH_LE_GATT_SERVICE service;
};

static inline struct gatt_service *impl_from_IGattDeviceService( IGattDeviceService *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_service, IGattDeviceService_iface );
}

static HRESULT WINAPI gatt_service_QueryInterface( IGattDeviceService *iface, REFIID iid, void **out )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattDeviceService ))
    {
        IGattDeviceService_AddRef(( *out = &impl->IGattDeviceService_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IGattDeviceService3 ))
    {
        IGattDeviceService3_AddRef(( *out = &impl->IGattDeviceService3_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        IClosable_AddRef(( *out = &impl->IClosable_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_service_AddRef( IGattDeviceService *iface )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "(%p) ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI gatt_service_Release( IGattDeviceService *iface )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p) ref %lu\n", iface, ref );

    if (!ref)
    {
        IBluetoothLEDevice_Release( &impl->device->IBluetoothLEDevice_iface );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( gatt_service, IGattDeviceService, RuntimeClass_Windows_Devices_Bluetooth_GenericAttributeProfile_GattDeviceService )

/* Get the characteristics of the service, optionally only those with the given UUID. */
static HRESULT gatt_service_get_characteristics( struct gatt_service *impl, const GUID *uuid,
                                                 IVectorView_GattCharacteristic **view )
{
    struct winebth_le_device_get_gatt_characteristics_params *params = NULL;
    IVector_IInspectable *vector;
    ULONG count = 16, i;
    DWORD size;
    HRESULT hr;

    for (;;)
    {
        size = offsetof( struct winebth_le_device_get_gatt_characteristics_params, characteristics[count] );
        free( params );
        if (!(params = calloc( 1, size ))) return E_OUTOFMEMORY;
        params->service = impl->service;
        if (ble_device_ioctl( impl->device, IOCTL_WINEBTH_LE_DEVICE_GET_GATT_CHARACTERISTICS, params, size, params,
                              size, NULL ))
            break;
        if (GetLastError() != ERROR_MORE_DATA || params->count <= count)
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            free( params );
            return hr;
        }
        count = params->count;
    }

    if (FAILED(hr = vector_create( &characteristic_vector_iids, (void **)&vector )))
    {
        free( params );
        return hr;
    }
    for (i = 0; i < params->count && SUCCEEDED(hr); i++)
    {
        IGattCharacteristic *characteristic;
        GUID char_uuid;

        le_uuid_to_guid( &params->characteristics[i].CharacteristicUuid, &char_uuid );
        if (uuid && !IsEqualGUID( uuid, &char_uuid )) continue;
        if (FAILED(hr = gatt_characteristic_create( impl->device, &impl->IGattDeviceService_iface,
                                                    &params->characteristics[i], &characteristic )))
            break;
        hr = IVector_IInspectable_Append( vector, (IInspectable *)characteristic );
        IGattCharacteristic_Release( characteristic );
    }
    free( params );
    if (SUCCEEDED(hr)) hr = IVector_IInspectable_GetView( vector, (IVectorView_IInspectable **)view );
    IVector_IInspectable_Release( vector );
    return hr;
}

static HRESULT WINAPI gatt_service_GetCharacteristics( IGattDeviceService *iface, GUID uuid, IVectorView_GattCharacteristic **chars )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), chars );

    if (!chars) return E_POINTER;
    return gatt_service_get_characteristics( impl, &uuid, chars );
}

static HRESULT WINAPI gatt_service_GetIncludedServices( IGattDeviceService *iface, GUID uuid, IVectorView_GattDeviceService **services )
{
    FIXME( "(%p, %s, %p): semi-stub!\n", iface, debugstr_guid( &uuid ), services );

    if (!services) return E_POINTER;
    return empty_view_create( &service_vector_iids, (void **)services );
}

static HRESULT WINAPI gatt_service_get_DeviceId( IGattDeviceService *iface, HSTRING *value )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->device->id, value );
}

static HRESULT WINAPI gatt_service_get_Uuid( IGattDeviceService *iface, GUID *value )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    le_uuid_to_guid( &impl->service.ServiceUuid, value );
    return S_OK;
}

static HRESULT WINAPI gatt_service_get_AttributeHandle( IGattDeviceService *iface, UINT16 *value )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->service.AttributeHandle;
    return S_OK;
}

static const IGattDeviceServiceVtbl gatt_service_vtbl =
{
    /* IUnknown */
    gatt_service_QueryInterface,
    gatt_service_AddRef,
    gatt_service_Release,
    /* IInspectable */
    gatt_service_GetIids,
    gatt_service_GetRuntimeClassName,
    gatt_service_GetTrustLevel,
    /* IGattDeviceService */
    gatt_service_GetCharacteristics,
    gatt_service_GetIncludedServices,
    gatt_service_get_DeviceId,
    gatt_service_get_Uuid,
    gatt_service_get_AttributeHandle
};

DEFINE_IINSPECTABLE( gatt_service3, IGattDeviceService3, struct gatt_service, IGattDeviceService_iface )

static HRESULT WINAPI gatt_service3_get_DeviceAccessInformation( IGattDeviceService3 *iface, IDeviceAccessInformation **value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_get_Session( IGattDeviceService3 *iface, IGattSession **value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_get_SharingMode( IGattDeviceService3 *iface, GattSharingMode *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = GattSharingMode_SharedReadAndWrite;
    return S_OK;
}

static HRESULT gatt_service_request_access_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    if (!called_async) return STATUS_PENDING;
    result->vt = VT_I4;
    result->lVal = DeviceAccessStatus_Allowed;
    return S_OK;
}

static HRESULT WINAPI gatt_service3_RequestAccessAsync( IGattDeviceService3 *iface, IAsyncOperation_DeviceAccessStatus **async )
{
    struct gatt_service *impl = impl_from_IGattDeviceService3( iface );

    TRACE( "(%p, %p)\n", iface, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_DeviceAccessStatus, &impl->IGattDeviceService_iface,
                                  async_params_create( NULL, BluetoothCacheMode_Cached ),
                                  gatt_service_request_access_async, TRUE, async );
}

static HRESULT gatt_service_open_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    if (!called_async) return STATUS_PENDING;
    result->vt = VT_I4;
    result->lVal = GattOpenStatus_Success;
    return S_OK;
}

static HRESULT WINAPI gatt_service3_OpenAsync( IGattDeviceService3 *iface, GattSharingMode mode, IAsyncOperation_GattOpenStatus **async )
{
    struct gatt_service *impl = impl_from_IGattDeviceService3( iface );

    FIXME( "(%p, %d, %p): semi-stub!\n", iface, mode, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattOpenStatus, &impl->IGattDeviceService_iface,
                                  async_params_create( NULL, BluetoothCacheMode_Cached ), gatt_service_open_async, TRUE, async );
}

static HRESULT gatt_service_get_characteristics_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( (IGattDeviceService *)invoker );
    GattCommunicationStatus status = GattCommunicationStatus_Success;
    struct async_params *params = impl_from_IUnknown( param );
    IGattCharacteristicsResult *characteristics_result;
    IVectorView_GattCharacteristic *view = NULL;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;

    if (FAILED(ble_device_connect( impl->device )) ||
        FAILED(gatt_service_get_characteristics( impl, params->has_uuid ? &params->uuid : NULL, &view )))
        status = GattCommunicationStatus_Unreachable;
    if (FAILED(hr = characteristics_result_create( status, view, &characteristics_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)characteristics_result;
    return S_OK;
}

static HRESULT gatt_service_get_characteristics_start( IGattDeviceService3 *iface, const GUID *uuid, BluetoothCacheMode mode,
                                                       IAsyncOperation_GattCharacteristicsResult **async )
{
    struct gatt_service *impl = impl_from_IGattDeviceService3( iface );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattCharacteristicsResult, &impl->IGattDeviceService_iface,
                                  async_params_create( uuid, mode ), gatt_service_get_characteristics_async, FALSE, async );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsAsync( IGattDeviceService3 *iface, IAsyncOperation_GattCharacteristicsResult **async )
{
    TRACE( "(%p, %p)\n", iface, async );
    return gatt_service_get_characteristics_start( iface, NULL, BluetoothCacheMode_Cached, async );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsWithCacheModeAsync( IGattDeviceService3 *iface, BluetoothCacheMode mode,
                                                                          IAsyncOperation_GattCharacteristicsResult **async )
{
    TRACE( "(%p, %d, %p)\n", iface, mode, async );
    return gatt_service_get_characteristics_start( iface, NULL, mode, async );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsForUuidAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                    IAsyncOperation_GattCharacteristicsResult **async )
{
    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), async );
    return gatt_service_get_characteristics_start( iface, &uuid, BluetoothCacheMode_Cached, async );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsForUuidWithCacheModeAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                                 BluetoothCacheMode mode,
                                                                                 IAsyncOperation_GattCharacteristicsResult **async )
{
    TRACE( "(%p, %s, %d, %p)\n", iface, debugstr_guid( &uuid ), mode, async );
    return gatt_service_get_characteristics_start( iface, &uuid, mode, async );
}

static HRESULT gatt_service_get_included_services_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    IGattDeviceServicesResult *services_result;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    /* TODO: BlueZ lists the included services in the Includes property of the service. */
    if (FAILED(hr = gatt_services_result_create( GattCommunicationStatus_Success, NULL, &services_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)services_result;
    return S_OK;
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                                  BluetoothCacheMode mode,
                                                                                  IAsyncOperation_GattDeviceServicesResult **async )
{
    struct gatt_service *impl = impl_from_IGattDeviceService3( iface );

    FIXME( "(%p, %s, %d, %p): semi-stub!\n", iface, debugstr_guid( &uuid ), mode, async );

    if (!async) return E_POINTER;
    return async_operation_start( &IID_IAsyncOperation_GattDeviceServicesResult, &impl->IGattDeviceService_iface,
                                  async_params_create( &uuid, mode ), gatt_service_get_included_services_async, FALSE, async );
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesAsync( IGattDeviceService3 *iface, IAsyncOperation_GattDeviceServicesResult **async )
{
    return gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync( iface, null_uuid, BluetoothCacheMode_Cached, async );
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesWithCacheModeAsync( IGattDeviceService3 *iface, BluetoothCacheMode mode,
                                                                           IAsyncOperation_GattDeviceServicesResult **async )
{
    return gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync( iface, null_uuid, mode, async );
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesForUuidAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                     IAsyncOperation_GattDeviceServicesResult **async )
{
    return gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync( iface, uuid, BluetoothCacheMode_Cached, async );
}

static const IGattDeviceService3Vtbl gatt_service3_vtbl =
{
    /* IUnknown */
    gatt_service3_QueryInterface,
    gatt_service3_AddRef,
    gatt_service3_Release,
    /* IInspectable */
    gatt_service3_GetIids,
    gatt_service3_GetRuntimeClassName,
    gatt_service3_GetTrustLevel,
    /* IGattDeviceService3 */
    gatt_service3_get_DeviceAccessInformation,
    gatt_service3_get_Session,
    gatt_service3_get_SharingMode,
    gatt_service3_RequestAccessAsync,
    gatt_service3_OpenAsync,
    gatt_service3_GetCharacteristicsAsync,
    gatt_service3_GetCharacteristicsWithCacheModeAsync,
    gatt_service3_GetCharacteristicsForUuidAsync,
    gatt_service3_GetCharacteristicsForUuidWithCacheModeAsync,
    gatt_service3_GetIncludedServicesAsync,
    gatt_service3_GetIncludedServicesWithCacheModeAsync,
    gatt_service3_GetIncludedServicesForUuidAsync,
    gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync,
};

DEFINE_IINSPECTABLE( gatt_service_closable, IClosable, struct gatt_service, IGattDeviceService_iface )

static HRESULT WINAPI gatt_service_closable_Close( IClosable *iface )
{
    /* The connection belongs to the device, which the service keeps alive until it is released. */
    TRACE( "(%p)\n", iface );
    return S_OK;
}

static const IClosableVtbl gatt_service_closable_vtbl =
{
    /* IUnknown */
    gatt_service_closable_QueryInterface,
    gatt_service_closable_AddRef,
    gatt_service_closable_Release,
    /* IInspectable */
    gatt_service_closable_GetIids,
    gatt_service_closable_GetRuntimeClassName,
    gatt_service_closable_GetTrustLevel,
    /* IClosable */
    gatt_service_closable_Close,
};

HRESULT gatt_service_create( struct ble_device *device, const BTH_LE_GATT_SERVICE *svc, IGattDeviceService **service )
{
    struct gatt_service *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IGattDeviceService_iface.lpVtbl = &gatt_service_vtbl;
    impl->IGattDeviceService3_iface.lpVtbl = &gatt_service3_vtbl;
    impl->IClosable_iface.lpVtbl = &gatt_service_closable_vtbl;
    impl->ref = 1;
    impl->service = *svc;
    IBluetoothLEDevice_AddRef( &(impl->device = device)->IBluetoothLEDevice_iface );
    *service = &impl->IGattDeviceService_iface;
    return S_OK;
}
