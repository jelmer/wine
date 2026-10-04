/* BluetoothDevice, BluetoothLEDevice Implementation
 *
 * Copyright 2025 Vibhav Pant
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
#include "setupapi.h"
#include "initguid.h"
#include "devpkey.h"
#include "bthledef.h"
#include "ddk/bthguid.h"
#include "bluetoothleapis.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetooth );

struct bluetoothdevice_statics
{
    IActivationFactory IActivationFactory_iface;
    IBluetoothDeviceStatics IBluetoothDeviceStatics_iface;
    LONG ref;
};

static inline struct bluetoothdevice_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct bluetoothdevice_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct bluetoothdevice_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "(%p, %s, %p) %ld\n", iface, debugstr_guid( iid ), out, impl->ref );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef(( *out = &impl->IActivationFactory_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBluetoothDeviceStatics))
    {
        IBluetoothDeviceStatics_AddRef(( *out = &impl->IBluetoothDeviceStatics_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct bluetoothdevice_statics *impl = impl_from_IActivationFactory( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct bluetoothdevice_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "(%p, %p): stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory */
    factory_ActivateInstance
};

DEFINE_IINSPECTABLE( bluetoothdevice_statics, IBluetoothDeviceStatics, struct bluetoothdevice_statics, IActivationFactory_iface );

static HRESULT WINAPI bluetoothdevice_statics_FromIdAsync( IBluetoothDeviceStatics *iface, HSTRING id, IAsyncOperation_BluetoothDevice **async_op )
{
    FIXME( "(%p, %s, %p): stub!\n", iface, debugstr_hstring( id ), async_op );
    return E_NOTIMPL;
}

static HRESULT WINAPI bluetoothdevice_statics_FromHostNameAsync( IBluetoothDeviceStatics *iface, IHostName *name, IAsyncOperation_BluetoothDevice **async_op )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, name, async_op );
    return E_NOTIMPL;
}

static HRESULT WINAPI bluetoothdevice_statics_FromBluetoothAddressAsync( IBluetoothDeviceStatics *iface,
                                                                         UINT64 address,
                                                                         IAsyncOperation_BluetoothDevice **async_op )
{
    FIXME( "(%p, %#I64x, %p): stub!\n", iface, address, async_op );
    return E_NOTIMPL;
}

static HRESULT WINAPI bluetoothdevice_statics_GetDeviceSelector( IBluetoothDeviceStatics *iface, HSTRING *value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    *value = NULL;
    return E_NOTIMPL;
}

static const IBluetoothDeviceStaticsVtbl bluetoothdevice_statics_vtbl =
{
    bluetoothdevice_statics_QueryInterface,
    bluetoothdevice_statics_AddRef,
    bluetoothdevice_statics_Release,
    /* IInspectable */
    bluetoothdevice_statics_GetIids,
    bluetoothdevice_statics_GetRuntimeClassName,
    bluetoothdevice_statics_GetTrustLevel,
    /* IBluetoothDeviceStatics */
    bluetoothdevice_statics_FromIdAsync,
    bluetoothdevice_statics_FromHostNameAsync,
    bluetoothdevice_statics_FromBluetoothAddressAsync,
    bluetoothdevice_statics_GetDeviceSelector,
};

struct device_id
{
    IBluetoothDeviceId IBluetoothDeviceId_iface;
    LONG ref;

    HSTRING id;
};

static inline struct device_id *impl_from_IBluetoothDeviceId( IBluetoothDeviceId *iface )
{
    return CONTAINING_RECORD( iface, struct device_id, IBluetoothDeviceId_iface );
}

static HRESULT WINAPI device_id_QueryInterface( IBluetoothDeviceId *iface, REFIID iid, void **out )
{
    struct device_id *impl = impl_from_IBluetoothDeviceId( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothDeviceId ))
    {
        IBluetoothDeviceId_AddRef(( *out = &impl->IBluetoothDeviceId_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI device_id_AddRef( IBluetoothDeviceId *iface )
{
    struct device_id *impl = impl_from_IBluetoothDeviceId( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI device_id_Release( IBluetoothDeviceId *iface )
{
    struct device_id *impl = impl_from_IBluetoothDeviceId( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->id );
        free( impl );
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( device_id, IBluetoothDeviceId, RuntimeClass_Windows_Devices_Bluetooth_BluetoothDeviceId )

static HRESULT WINAPI device_id_get_Id( IBluetoothDeviceId *iface, HSTRING *value )
{
    struct device_id *impl = impl_from_IBluetoothDeviceId( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->id, value );
}

static HRESULT WINAPI device_id_get_IsClassicDevice( IBluetoothDeviceId *iface, boolean *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI device_id_get_IsLowEnergyDevice( IBluetoothDeviceId *iface, boolean *value )
{
    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static const IBluetoothDeviceIdVtbl device_id_vtbl =
{
    /* IUnknown */
    device_id_QueryInterface,
    device_id_AddRef,
    device_id_Release,
    /* IInspectable */
    device_id_GetIids,
    device_id_GetRuntimeClassName,
    device_id_GetTrustLevel,
    /* IBluetoothDeviceId */
    device_id_get_Id,
    device_id_get_IsClassicDevice,
    device_id_get_IsLowEnergyDevice,
};

static HRESULT le_device_id_create( HSTRING id, IBluetoothDeviceId **out )
{
    struct device_id *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = WindowsDuplicateString( id, &impl->id )))
    {
        free( impl );
        return hr;
    }
    impl->IBluetoothDeviceId_iface.lpVtbl = &device_id_vtbl;
    impl->ref = 1;
    *out = &impl->IBluetoothDeviceId_iface;
    return S_OK;
}

static const struct vector_iids gatt_service_vector_iids =
{
    .vector = &IID_IVector_IInspectable,
    .view = &IID_IVectorView_GattDeviceService,
    .iterable = &IID_IIterable_GattDeviceService,
    .iterator = &IID_IIterator_GattDeviceService,
};

static inline struct ble_device *impl_from_IBluetoothLEDevice( IBluetoothLEDevice *iface )
{
    return CONTAINING_RECORD( iface, struct ble_device, IBluetoothLEDevice_iface );
}

BOOL ble_device_ioctl( struct ble_device *device, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size,
                       DWORD *ret_size )
{
    HANDLE handle;
    DWORD bytes;

    EnterCriticalSection( &device->cs );
    handle = device->device;
    LeaveCriticalSection( &device->cs );

    if (handle == INVALID_HANDLE_VALUE)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    return DeviceIoControl( handle, code, in, in_size, out, out_size, ret_size ? ret_size : &bytes, NULL );
}

HRESULT ble_device_get_info( struct ble_device *device, struct winebth_le_device_info *info )
{
    if (!ble_device_ioctl( device, IOCTL_WINEBTH_LE_DEVICE_GET_INFO, NULL, 0, info, sizeof( *info ), NULL ))
        return HRESULT_FROM_WIN32( GetLastError() );
    return S_OK;
}

/* Connect to the device, and wait until its GATT services are known. */
HRESULT ble_device_connect( struct ble_device *device )
{
    struct winebth_le_device_info info;
    DWORD attempt = 0, end;
    HRESULT hr;

    /* Connection attempts get aborted at times, in particular while the radio is scanning. */
    while (!ble_device_ioctl( device, IOCTL_WINEBTH_LE_DEVICE_CONNECT, NULL, 0, NULL, 0, NULL ))
    {
        WARN( "Failed to connect to %I64x: %lu\n", device->addr, GetLastError() );
        if (GetLastError() == ERROR_INVALID_HANDLE || ++attempt == 3) return HRESULT_FROM_WIN32( ERROR_DEVICE_NOT_CONNECTED );
        Sleep( 500 );
    }

    end = GetTickCount() + 30000;
    for (;;)
    {
        if (FAILED(hr = ble_device_get_info( device, &info ))) return hr;
        if (info.services_resolved) return S_OK;
        if (!info.connected || (int)(end - GetTickCount()) <= 0) return HRESULT_FROM_WIN32( ERROR_DEVICE_NOT_CONNECTED );
        Sleep( 100 );
    }
}

/* Get the primary services of the device, optionally only those with the given UUID. */
HRESULT ble_device_get_gatt_services( struct ble_device *device, const GUID *uuid, IVectorView_GattDeviceService **view )
{
    struct winebth_le_device_get_gatt_services_params *params = NULL;
    IVector_IInspectable *vector;
    ULONG count = 16, i;
    DWORD size;
    HRESULT hr;

    for (;;)
    {
        size = offsetof( struct winebth_le_device_get_gatt_services_params, services[count] );
        free( params );
        if (!(params = calloc( 1, size ))) return E_OUTOFMEMORY;
        if (ble_device_ioctl( device, IOCTL_WINEBTH_LE_DEVICE_GET_GATT_SERVICES, NULL, 0, params, size, NULL )) break;
        if (GetLastError() != ERROR_MORE_DATA || params->count <= count)
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            free( params );
            return hr;
        }
        count = params->count;
    }

    if (FAILED(hr = vector_create( &gatt_service_vector_iids, (void **)&vector )))
    {
        free( params );
        return hr;
    }
    for (i = 0; i < params->count && SUCCEEDED(hr); i++)
    {
        const BTH_LE_UUID *le_uuid = &params->services[i].ServiceUuid;
        IGattDeviceService *service;
        GUID service_uuid;

        if (le_uuid->IsShortUuid)
        {
            service_uuid = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
            service_uuid.Data1 = le_uuid->Value.ShortUuid;
        }
        else
            service_uuid = le_uuid->Value.LongUuid;
        if (uuid && !IsEqualGUID( uuid, &service_uuid )) continue;
        if (FAILED(hr = gatt_service_create( device, &params->services[i], &service ))) break;
        hr = IVector_IInspectable_Append( vector, (IInspectable *)service );
        IGattDeviceService_Release( service );
    }
    free( params );
    if (SUCCEEDED(hr)) hr = IVector_IInspectable_GetView( vector, (IVectorView_IInspectable **)view );
    IVector_IInspectable_Release( vector );
    return hr;
}

static void ble_device_destroy( struct ble_device *impl )
{
    if (impl->event_thread) CloseHandle( impl->event_thread );
    if (impl->stop_event) CloseHandle( impl->stop_event );
    if (impl->event_device != INVALID_HANDLE_VALUE) CloseHandle( impl->event_device );
    if (impl->device != INVALID_HANDLE_VALUE) CloseHandle( impl->device );
    event_handlers_clear( &impl->name_handlers );
    event_handlers_clear( &impl->services_handlers );
    event_handlers_clear( &impl->connection_handlers );
    WindowsDeleteString( impl->id );
    impl->cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection( &impl->cs );
    free( impl );
}

/* Take a reference, unless the device is being destroyed. */
static BOOL ble_device_try_addref( struct ble_device *impl )
{
    LONG ref;

    do
    {
        if (!(ref = impl->ref)) return FALSE;
    } while (InterlockedCompareExchange( &impl->ref, ref + 1, ref ) != ref);
    return TRUE;
}

static DWORD WINAPI ble_device_event_thread( void *arg )
{
    BYTE buffer[offsetof( struct winebth_le_device_event, buf[512] )];
    struct winebth_le_device_event *event = (struct winebth_le_device_event *)buffer;
    struct ble_device *impl = arg;
    OVERLAPPED ovl = {0};
    HANDLE handles[2];
    DWORD bytes;

    SetThreadDescription( GetCurrentThread(), L"wine_bluetooth_le_device_events" );
    RoInitialize( RO_INIT_MULTITHREADED );

    ovl.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    handles[0] = impl->stop_event;
    handles[1] = ovl.hEvent;

    for (;;)
    {
        if (!DeviceIoControl( impl->event_device, IOCTL_WINEBTH_LE_DEVICE_READ_EVENT, NULL, 0, buffer, sizeof( buffer ),
                              &bytes, &ovl ))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                WARN( "Failed to read event: %lu\n", GetLastError() );
                break;
            }
            if (WaitForMultipleObjects( 2, handles, FALSE, INFINITE ) == WAIT_OBJECT_0)
            {
                CancelIoEx( impl->event_device, &ovl );
                GetOverlappedResult( impl->event_device, &ovl, &bytes, TRUE );
                break;
            }
            if (!GetOverlappedResult( impl->event_device, &ovl, &bytes, FALSE ))
            {
                WARN( "Failed to read event: %lu\n", GetLastError() );
                break;
            }
        }
        if (bytes < offsetof( struct winebth_le_device_event, buf )) continue;
        if (!ble_device_try_addref( impl )) break;

        switch (event->type)
        {
        case WINEBTH_LE_DEVICE_EVENT_CONNECTION_CHANGED:
            TRACE( "%I64x connected %d\n", impl->addr, event->connected );
            event_handlers_notify( &impl->connection_handlers, (IInspectable *)&impl->IBluetoothLEDevice_iface, NULL );
            break;
        case WINEBTH_LE_DEVICE_EVENT_VALUE_CHANGED:
            if (bytes < offsetof( struct winebth_le_device_event, buf[event->size] )) break;
            gatt_characteristics_value_changed( impl, event->handle, event->buf, event->size );
            break;
        default:
            FIXME( "Unknown event type %lu\n", event->type );
        }

        IBluetoothLEDevice_Release( &impl->IBluetoothLEDevice_iface );
        if (impl->detached) break;
    }

    CloseHandle( ovl.hEvent );
    RoUninitialize();
    /* The last reference was released from one of the handlers called above. */
    if (impl->detached) ble_device_destroy( impl );
    return 0;
}

static HRESULT WINAPI ble_device_QueryInterface( IBluetoothLEDevice *iface, REFIID iid, void **out )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEDevice ))
    {
        IBluetoothLEDevice_AddRef( (*out = &impl->IBluetoothLEDevice_iface) );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBluetoothLEDevice3 ))
    {
        IBluetoothLEDevice3_AddRef( (*out = &impl->IBluetoothLEDevice3_iface) );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBluetoothLEDevice4 ))
    {
        IBluetoothLEDevice4_AddRef( (*out = &impl->IBluetoothLEDevice4_iface) );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        IClosable_AddRef( (*out = &impl->IClosable_iface) );
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI ble_device_AddRef( IBluetoothLEDevice *iface )
{
     struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
     TRACE( "(%p)\n", iface );
     return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI ble_device_Release( IBluetoothLEDevice *iface )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );

    if (!ref)
    {
        SetEvent( impl->stop_event );
        if (impl->event_thread && GetCurrentThreadId() == impl->event_thread_id)
            impl->detached = TRUE;
        else
        {
            if (impl->event_thread) WaitForSingleObject( impl->event_thread, INFINITE );
            ble_device_destroy( impl );
        }
    }
    return ref;
}

DEFINE_IINSPECTABLE_STUBS( ble_device, IBluetoothLEDevice, RuntimeClass_Windows_Devices_Bluetooth_BluetoothLEDevice )

static HRESULT WINAPI ble_device_get_DeviceId( IBluetoothLEDevice *iface, HSTRING *value )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
    TRACE( "(%p, %p)\n", iface, value );
    return WindowsDuplicateString( impl->id, value );
}

static HRESULT WINAPI ble_device_get_Name( IBluetoothLEDevice *iface, HSTRING *value )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
    struct winebth_le_device_info info;
    WCHAR name[BLUETOOTH_MAX_NAME_SIZE];
    HRESULT hr;
    int len;

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    if (FAILED(hr = ble_device_get_info( impl, &info ))) return hr;
    len = MultiByteToWideChar( CP_UTF8, 0, info.name, strnlen( info.name, sizeof( info.name ) ), name, ARRAY_SIZE( name ) );
    return WindowsCreateString( name, len, value );
}

static HRESULT WINAPI ble_device_get_GattServices( IBluetoothLEDevice *iface, IVectorView_GattDeviceService **services )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    TRACE( "(%p, %p)\n", iface, services );

    if (!services) return E_POINTER;
    return ble_device_get_gatt_services( impl, NULL, services );
}

static HRESULT WINAPI ble_device_get_ConnectionStatus( IBluetoothLEDevice *iface, BluetoothConnectionStatus *value )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
    struct winebth_le_device_info info;

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = BluetoothConnectionStatus_Disconnected;
    if (SUCCEEDED(ble_device_get_info( impl, &info )) && info.connected) *value = BluetoothConnectionStatus_Connected;
    return S_OK;
}

static HRESULT WINAPI ble_device_get_BluetoothAddress( IBluetoothLEDevice *iface, UINT64 *value )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
    TRACE( "(%p, %p)\n", iface, value );
    *value = impl->addr;
    return S_OK;
}

static HRESULT WINAPI ble_device_GetGattService( IBluetoothLEDevice *iface, GUID uuid, IGattDeviceService **service )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );
    IVectorView_GattDeviceService *services;
    UINT32 count = 0;
    HRESULT hr;

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), service );

    if (!service) return E_POINTER;
    *service = NULL;
    if (FAILED(hr = ble_device_get_gatt_services( impl, &uuid, &services ))) return hr;
    IVectorView_GattDeviceService_get_Size( services, &count );
    hr = count ? IVectorView_GattDeviceService_GetAt( services, 0, service ) : HRESULT_FROM_WIN32( ERROR_NOT_FOUND );
    IVectorView_GattDeviceService_Release( services );
    return hr;
}

static HRESULT WINAPI ble_device_add_NameChanged( IBluetoothLEDevice *iface, ITypedEventHandler_BluetoothLEDevice_IInspectable *handler,
                                                  EventRegistrationToken *token )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    /* TODO: the handlers are never called. */
    FIXME( "(%p, %p, %p): semi-stub!\n", iface, handler, token );

    if (!token) return E_POINTER;
    return event_handlers_append( &impl->name_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI ble_device_remove_NameChanged( IBluetoothLEDevice *iface, EventRegistrationToken token )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    TRACE( "(%p, %I64d)\n", iface, token.value );

    return event_handlers_remove( &impl->name_handlers, &token );
}

static HRESULT WINAPI ble_device_add_GattServicesChanged( IBluetoothLEDevice *iface, ITypedEventHandler_BluetoothLEDevice_IInspectable *handler,
                                                          EventRegistrationToken *token )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    /* TODO: the handlers are never called. */
    FIXME( "(%p, %p, %p): semi-stub!\n", iface, handler, token );

    if (!token) return E_POINTER;
    return event_handlers_append( &impl->services_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI ble_device_remove_GattServicesChanged( IBluetoothLEDevice *iface, EventRegistrationToken token )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    TRACE( "(%p, %I64d)\n", iface, token.value );

    return event_handlers_remove( &impl->services_handlers, &token );
}

static HRESULT WINAPI ble_device_add_ConnectionStatusChanged( IBluetoothLEDevice *iface, ITypedEventHandler_BluetoothLEDevice_IInspectable *handler,
                                                              EventRegistrationToken *token )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    if (!token) return E_POINTER;
    return event_handlers_append( &impl->connection_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI ble_device_remove_ConnectionStatusChanged( IBluetoothLEDevice *iface, EventRegistrationToken token )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( iface );

    TRACE( "(%p, %I64d)\n", iface, token.value );

    return event_handlers_remove( &impl->connection_handlers, &token );
}

static const IBluetoothLEDeviceVtbl ble_device_vtbl = {
    /* IUnknown */
    ble_device_QueryInterface,
    ble_device_AddRef,
    ble_device_Release,
    /* IInspectable */
    ble_device_GetIids,
    ble_device_GetRuntimeClassName,
    ble_device_GetTrustLevel,
    /* IBluetoothLEDevice */
    ble_device_get_DeviceId,
    ble_device_get_Name,
    ble_device_get_GattServices,
    ble_device_get_ConnectionStatus,
    ble_device_get_BluetoothAddress,
    ble_device_GetGattService,
    ble_device_add_NameChanged,
    ble_device_remove_NameChanged,
    ble_device_add_GattServicesChanged,
    ble_device_remove_GattServicesChanged,
    ble_device_add_ConnectionStatusChanged,
    ble_device_remove_ConnectionStatusChanged,
};

DEFINE_IINSPECTABLE( ble_device3, IBluetoothLEDevice3, struct ble_device, IBluetoothLEDevice_iface )

static HRESULT WINAPI ble_device3_get_DeviceAccessInformation( IBluetoothLEDevice3 *iface, IDeviceAccessInformation **value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT ble_device_request_access_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    if (!called_async) return STATUS_PENDING;
    result->vt = VT_I4;
    result->lVal = DeviceAccessStatus_Allowed;
    return S_OK;
}

static HRESULT WINAPI ble_device3_RequestAccessAsync( IBluetoothLEDevice3 *iface, IAsyncOperation_DeviceAccessStatus **operation )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice3( iface );

    TRACE( "(%p, %p)\n", iface, operation );

    if (!operation) return E_POINTER;
    return async_operation_value_create( &IID_IAsyncOperation_DeviceAccessStatus, (IUnknown *)&impl->IBluetoothLEDevice_iface,
                                         NULL, ble_device_request_access_async, (IAsyncOperation_IInspectable **)operation );
}

static HRESULT ble_device_get_services_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice( (IBluetoothLEDevice *)invoker );
    GattCommunicationStatus status = GattCommunicationStatus_Success;
    IVectorView_GattDeviceService *services = NULL;
    IGattDeviceServicesResult *services_result;
    GUID uuid, *filter = NULL;
    DWORD attempt;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;

    if (param && SUCCEEDED(IPropertyValue_GetGuid( (IPropertyValue *)param, &uuid ))) filter = &uuid;

    if (FAILED(hr = ble_device_connect( impl )))
        status = GattCommunicationStatus_Unreachable;
    else
    {
        /* The service objects may show up slightly later than the notice that they have been resolved. */
        for (attempt = 0; attempt < 20; attempt++)
        {
            UINT32 count = 0;

            if (services) IVectorView_GattDeviceService_Release( services );
            services = NULL;
            if (FAILED(hr = ble_device_get_gatt_services( impl, NULL, &services ))) break;
            IVectorView_GattDeviceService_get_Size( services, &count );
            if (count) break;
            Sleep( 100 );
        }
        if (SUCCEEDED(hr) && filter)
        {
            IVectorView_GattDeviceService_Release( services );
            services = NULL;
            hr = ble_device_get_gatt_services( impl, filter, &services );
        }
        if (FAILED(hr)) status = GattCommunicationStatus_Unreachable;
    }

    if (FAILED(hr = gatt_services_result_create( status, services, &services_result ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)services_result;
    return S_OK;
}

static HRESULT ble_device_get_services_start( IBluetoothLEDevice3 *iface, const GUID *uuid,
                                              IAsyncOperation_GattDeviceServicesResult **operation )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Foundation_PropertyValue;
    struct ble_device *impl = impl_from_IBluetoothLEDevice3( iface );
    IPropertyValueStatics *statics;
    IInspectable *param = NULL;
    HSTRING_HEADER hdr;
    HSTRING str;
    HRESULT hr;

    if (!operation) return E_POINTER;
    if (uuid)
    {
        if (FAILED(hr = WindowsCreateStringReference( class_name, wcslen( class_name ), &hdr, &str ))) return hr;
        if (FAILED(hr = RoGetActivationFactory( str, &IID_IPropertyValueStatics, (void **)&statics ))) return hr;
        hr = IPropertyValueStatics_CreateGuid( statics, *uuid, &param );
        IPropertyValueStatics_Release( statics );
        if (FAILED(hr)) return hr;
    }
    hr = async_operation_inspectable_create( &IID_IAsyncOperation_GattDeviceServicesResult,
                                             (IUnknown *)&impl->IBluetoothLEDevice_iface, (IUnknown *)param,
                                             ble_device_get_services_async, (IAsyncOperation_IInspectable **)operation );
    if (param) IInspectable_Release( param );
    return hr;
}

static HRESULT WINAPI ble_device3_GetGattServicesAsync( IBluetoothLEDevice3 *iface, IAsyncOperation_GattDeviceServicesResult **operation )
{
    TRACE( "(%p, %p)\n", iface, operation );
    return ble_device_get_services_start( iface, NULL, operation );
}

static HRESULT WINAPI ble_device3_GetGattServicesWithCacheModeAsync( IBluetoothLEDevice3 *iface, BluetoothCacheMode mode,
                                                                     IAsyncOperation_GattDeviceServicesResult **operation )
{
    TRACE( "(%p, %d, %p)\n", iface, mode, operation );
    return ble_device_get_services_start( iface, NULL, operation );
}

static HRESULT WINAPI ble_device3_GetGattServicesForUuidAsync( IBluetoothLEDevice3 *iface, GUID uuid,
                                                               IAsyncOperation_GattDeviceServicesResult **operation )
{
    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), operation );
    return ble_device_get_services_start( iface, &uuid, operation );
}

static HRESULT WINAPI ble_device3_GetGattServicesForUuidWithCacheModeAsync( IBluetoothLEDevice3 *iface, GUID uuid, BluetoothCacheMode mode,
                                                                            IAsyncOperation_GattDeviceServicesResult **operation )
{
    TRACE( "(%p, %s, %d, %p)\n", iface, debugstr_guid( &uuid ), mode, operation );
    return ble_device_get_services_start( iface, &uuid, operation );
}

static const IBluetoothLEDevice3Vtbl ble_device3_vtbl =
{
    /* IUnknown */
    ble_device3_QueryInterface,
    ble_device3_AddRef,
    ble_device3_Release,
    /* IInspectable */
    ble_device3_GetIids,
    ble_device3_GetRuntimeClassName,
    ble_device3_GetTrustLevel,
    /* IBluetoothLEDevice3 */
    ble_device3_get_DeviceAccessInformation,
    ble_device3_RequestAccessAsync,
    ble_device3_GetGattServicesAsync,
    ble_device3_GetGattServicesWithCacheModeAsync,
    ble_device3_GetGattServicesForUuidAsync,
    ble_device3_GetGattServicesForUuidWithCacheModeAsync,
};

DEFINE_IINSPECTABLE( ble_device4, IBluetoothLEDevice4, struct ble_device, IBluetoothLEDevice_iface )

static HRESULT WINAPI ble_device4_get_BluetoothDeviceId( IBluetoothLEDevice4 *iface, IBluetoothDeviceId **value )
{
    struct ble_device *impl = impl_from_IBluetoothLEDevice4( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    return le_device_id_create( impl->id, value );
}

static const IBluetoothLEDevice4Vtbl ble_device4_vtbl =
{
    /* IUnknown */
    ble_device4_QueryInterface,
    ble_device4_AddRef,
    ble_device4_Release,
    /* IInspectable */
    ble_device4_GetIids,
    ble_device4_GetRuntimeClassName,
    ble_device4_GetTrustLevel,
    /* IBluetoothLEDevice4 */
    ble_device4_get_BluetoothDeviceId,
};

DEFINE_IINSPECTABLE( ble_device_closable, IClosable, struct ble_device, IBluetoothLEDevice_iface )

static HRESULT WINAPI ble_device_closable_Close( IClosable *iface )
{
    struct ble_device *impl = impl_from_IClosable( iface );
    HANDLE device;

    TRACE( "(%p)\n", iface );

    /* The driver drops the connection once the handles that asked for it are closed. */
    EnterCriticalSection( &impl->cs );
    device = impl->device;
    impl->device = INVALID_HANDLE_VALUE;
    LeaveCriticalSection( &impl->cs );

    if (device != INVALID_HANDLE_VALUE) CloseHandle( device );
    SetEvent( impl->stop_event );
    return S_OK;
}

static const IClosableVtbl ble_device_closable_vtbl =
{
    /* IUnknown */
    ble_device_closable_QueryInterface,
    ble_device_closable_AddRef,
    ble_device_closable_Release,
    /* IInspectable */
    ble_device_closable_GetIids,
    ble_device_closable_GetRuntimeClassName,
    ble_device_closable_GetTrustLevel,
    /* IClosable */
    ble_device_closable_Close,
};

static HRESULT ble_device_create( IBluetoothLEDevice **device, const WCHAR *id, UINT64 addr )
{
    struct ble_device *impl;
    HRESULT hr;

    TRACE( "(%p, %s, %I64x)\n", device, debugstr_w( id ), addr );

    if (!(impl = calloc( 1, sizeof( *impl ))))
        return E_OUTOFMEMORY;
    impl->IBluetoothLEDevice_iface.lpVtbl = &ble_device_vtbl;
    impl->IBluetoothLEDevice3_iface.lpVtbl = &ble_device3_vtbl;
    impl->IBluetoothLEDevice4_iface.lpVtbl = &ble_device4_vtbl;
    impl->IClosable_iface.lpVtbl = &ble_device_closable_vtbl;
    impl->ref = 1;
    impl->addr = addr;
    impl->device = impl->event_device = INVALID_HANDLE_VALUE;
    list_init( &impl->characteristics );
    list_init( &impl->name_handlers );
    list_init( &impl->services_handlers );
    list_init( &impl->connection_handlers );
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": ble_device.cs");

    if (FAILED(hr = WindowsCreateString( id, wcslen( id ), &impl->id ))) goto failed;
    impl->device = CreateFileW( id, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL );
    if (impl->device == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto failed;
    }
    impl->event_device = CreateFileW( id, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                      FILE_FLAG_OVERLAPPED, NULL );
    if (impl->event_device == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto failed;
    }
    if (!(impl->stop_event = CreateEventW( NULL, TRUE, FALSE, NULL )) ||
        !(impl->event_thread = CreateThread( NULL, 0, ble_device_event_thread, impl, 0, &impl->event_thread_id )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto failed;
    }

    *device = &impl->IBluetoothLEDevice_iface;
    return S_OK;

failed:
    ble_device_destroy( impl );
    return hr;
}

/* Find the device interface of the LE device with the given address. */
static BOOL find_le_device_interface( UINT64 addr, SP_DEVICE_INTERFACE_DETAIL_DATA_W *iface_detail, DWORD size )
{
    SP_DEVICE_INTERFACE_DATA iface_data = { .cbSize = sizeof( iface_data ) };
    BOOL found = FALSE;
    HDEVINFO devinfo;
    DWORD idx = 0;

    devinfo = SetupDiGetClassDevsW( &GUID_BLUETOOTHLE_DEVICE_INTERFACE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE );
    if (devinfo == INVALID_HANDLE_VALUE) return FALSE;
    iface_detail->cbSize = sizeof( *iface_detail );
    while (!found && SetupDiEnumDeviceInterfaces( devinfo, NULL, &GUID_BLUETOOTHLE_DEVICE_INTERFACE, idx++, &iface_data ))
    {
        SP_DEVINFO_DATA devinfo_data = { .cbSize = sizeof( devinfo_data ) };
        WCHAR addr_str[13];
        DEVPROPTYPE type;
        UINT64 addr2 = 0;

        if (!SetupDiGetDeviceInterfaceDetailW( devinfo, &iface_data, iface_detail, size, NULL, &devinfo_data ))
            continue;
        if (!SetupDiGetDevicePropertyW( devinfo, &devinfo_data, &DEVPKEY_Bluetooth_DeviceAddress, &type, (BYTE *)addr_str, sizeof( addr_str ), NULL, 0 ))
            continue;
        if (swscanf( addr_str, L"%I64x", &addr2 ) != 1)
            continue;
        found = addr == addr2;
    }
    SetupDiDestroyDeviceInfoList( devinfo );
    return found;
}

static HRESULT bluetoothledevice_get_device_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    char buffer[sizeof( SP_DEVICE_INTERFACE_DETAIL_DATA_W ) + MAX_PATH * sizeof( WCHAR )];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *iface_detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)buffer;
    IBluetoothLEDevice *device;
    DWORD attempt;
    UINT64 addr;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (FAILED(hr = IPropertyValue_GetUInt64( (IPropertyValue *)param, &addr )))
        return hr;

    result->vt = VT_NULL;
    /* A device that was only just discovered may not have been started by the PnP manager yet. */
    for (attempt = 0; !find_le_device_interface( addr, iface_detail, sizeof( buffer ) ); attempt++)
    {
        if (attempt == 20) return S_OK;
        Sleep( 100 );
    }
    if (FAILED(hr = ble_device_create( &device, iface_detail->DevicePath, addr )))
        return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)device;
    return S_OK;
}

struct bluetoothledevice_statics
{
    IActivationFactory IActivationFactory_iface;
    IBluetoothLEDeviceStatics IBluetoothLEDeviceStatics_iface;
    LONG ref;
};

static inline struct bluetoothledevice_statics *ble_device_impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct bluetoothledevice_statics, IActivationFactory_iface );
}

static HRESULT WINAPI ble_device_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct bluetoothledevice_statics *impl = ble_device_impl_from_IActivationFactory( iface );

    TRACE( "(%p, %s, %p) %ld\n", iface, debugstr_guid( iid ), out, impl->ref );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef(( *out = &impl->IActivationFactory_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBluetoothLEDeviceStatics ))
    {
        IBluetoothLEDeviceStatics_AddRef(( *out = &impl->IBluetoothLEDeviceStatics_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI ble_device_factory_AddRef( IActivationFactory *iface )
{
    struct bluetoothledevice_statics *impl = ble_device_impl_from_IActivationFactory( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI ble_device_factory_Release( IActivationFactory *iface )
{
    struct bluetoothledevice_statics *impl = ble_device_impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    return ref;
}

static HRESULT WINAPI ble_device_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI ble_device_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI ble_device_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI ble_device_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "(%p, %p): stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl ble_device_factory_vtbl =
{
    ble_device_factory_QueryInterface,
    ble_device_factory_AddRef,
    ble_device_factory_Release,
    /* IInspectable */
    ble_device_factory_GetIids,
    ble_device_factory_GetRuntimeClassName,
    ble_device_factory_GetTrustLevel,
    /* IActivationFactory */
    ble_device_factory_ActivateInstance
};

DEFINE_IINSPECTABLE( bluetoothledevice_statics, IBluetoothLEDeviceStatics, struct bluetoothledevice_statics, IActivationFactory_iface );

static HRESULT WINAPI bluetoothledevice_statics_FromIdAsync( IBluetoothLEDeviceStatics *iface, HSTRING id, IAsyncOperation_BluetoothLEDevice **async_op )
{
    FIXME( "(%p, %s, %p): stub!\n", iface, debugstr_hstring( id ), async_op );
    return E_NOTIMPL;
}

static HRESULT WINAPI bluetoothledevice_statics_FromBluetoothAddressAsync( IBluetoothLEDeviceStatics *iface,
                                                                           UINT64 addr,
                                                                           IAsyncOperation_BluetoothLEDevice **async_op )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Foundation_PropertyValue;
    IPropertyValueStatics *statics;
    IPropertyValue *addr_val;
    HSTRING_HEADER hdr;
    HSTRING str;
    HRESULT hr;

    TRACE( "(%p, %#I64x, %p)\n", iface, addr, async_op );

    if (FAILED(hr = WindowsCreateStringReference( class_name, wcslen( class_name ), &hdr, &str ))) return hr;
    if (FAILED(hr = RoGetActivationFactory( str, &IID_IPropertyValueStatics, (void **)&statics ))) return hr;
    if (FAILED(hr = IPropertyValueStatics_CreateUInt64( statics, addr, (IInspectable **)&addr_val )))
    {
        IPropertyValueStatics_Release( statics );
        return hr;
    }
    IPropertyValueStatics_Release( statics );

    return async_operation_inspectable_create( &IID_IAsyncOperation_BluetoothLEDevice, (IUnknown *)iface, (IUnknown *)addr_val, bluetoothledevice_get_device_async,
                                               (IAsyncOperation_IInspectable **)async_op );
}

static HRESULT WINAPI bluetoothledevice_statics_GetDeviceSelector( IBluetoothLEDeviceStatics *iface, HSTRING *result )
{
    FIXME( "(%p, %p): stub!\n", iface, result );
    return E_NOTIMPL;
}

static const IBluetoothLEDeviceStaticsVtbl bluetoothledevice_statics_vtbl =
{
    /* IUnknown */
    bluetoothledevice_statics_QueryInterface,
    bluetoothledevice_statics_AddRef,
    bluetoothledevice_statics_Release,
    /* IInspectable */
    bluetoothledevice_statics_GetIids,
    bluetoothledevice_statics_GetRuntimeClassName,
    bluetoothledevice_statics_GetTrustLevel,
    /* IBluetoothLEDeviceStatics */
    bluetoothledevice_statics_FromIdAsync,
    bluetoothledevice_statics_FromBluetoothAddressAsync,
    bluetoothledevice_statics_GetDeviceSelector
};


static struct bluetoothdevice_statics bluetoothdevice_statics =
{
    {&factory_vtbl},
    {&bluetoothdevice_statics_vtbl},
    1
};

IActivationFactory *bluetoothdevice_statics_factory = &bluetoothdevice_statics.IActivationFactory_iface;

static struct bluetoothledevice_statics bluetoothledevice_statics =
{
    {&ble_device_factory_vtbl},
    {&bluetoothledevice_statics_vtbl},
    1
};

IActivationFactory *bluetoothledevice_statics_factory = &bluetoothledevice_statics.IActivationFactory_iface;
