/* WinRT Windows.Devices.Bluetooth Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#ifndef __WINE_WINDOWS_DEVICES_BLUETOOTH_PRIVATE_H
#define __WINE_WINDOWS_DEVICES_BLUETOOTH_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "bthledef.h"

#include "wine/list.h"

#include "activation.h"
#include "bthdef.h"
#include "bthsdpdef.h"
#include "bluetoothapis.h"
#include "winioctl.h"
#include "wine/winebth.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"
#include "robuffer.h"
#define WIDL_using_Windows_Devices_Enumeration
#include "windows.devices.enumeration.h"
#define WIDL_using_Windows_Networking
#include "windows.networking.connectivity.h"
#include "windows.networking.h"
#define WIDL_using_Windows_Devices_Radios
#include "windows.devices.radios.h"
#define WIDL_using_Windows_Devices_Bluetooth
#define WIDL_using_Windows_Devices_Bluetooth_GenericAttributeProfile
#include "windows.devices.bluetooth.genericattributeprofile.h"
#include "windows.devices.bluetooth.rfcomm.h"
#include "windows.devices.bluetooth.h"
#define WIDL_using_Windows_Devices_Bluetooth_Advertisement
#include "windows.devices.bluetooth.advertisement.h"

#include "async_private.h"

extern IActivationFactory *bluetoothadapter_factory;
extern IActivationFactory *bluetoothdevice_statics_factory;
extern IActivationFactory *bluetoothledevice_statics_factory;
extern IActivationFactory *advertisement_watcher_factory;

HRESULT async_operation_inspectable_create( const GUID *iid, IUnknown *invoker, IUnknown *param,
                                            async_operation_callback callback, IAsyncOperation_IInspectable **out );

struct vector_iids
{
    const GUID *iterable;
    const GUID *iterator;
    const GUID *vector;
    const GUID *view;
};
HRESULT vector_create( const struct vector_iids *iids, void **out );

HRESULT async_operation_value_create( const GUID *iid, IUnknown *invoker, IUnknown *param,
                                      async_operation_callback callback, IAsyncOperation_IInspectable **out );

struct ble_device
{
    IBluetoothLEDevice IBluetoothLEDevice_iface;
    IBluetoothLEDevice3 IBluetoothLEDevice3_iface;
    IBluetoothLEDevice4 IBluetoothLEDevice4_iface;
    IClosable IClosable_iface;
    LONG ref;

    HSTRING id;
    UINT64 addr;

    CRITICAL_SECTION cs;
    HANDLE device;       /* Used for requests, which block until they complete. Guarded by cs. */
    HANDLE event_device; /* Used by the event thread to wait for events. */
    HANDLE event_thread;
    DWORD event_thread_id;
    HANDLE stop_event;
    BOOL detached;       /* Whether the event thread is the one that released the last reference. */

    struct list characteristics; /* struct gatt_characteristic, for value change events. Guarded by cs. */
    struct list name_handlers;
    struct list services_handlers;
    struct list connection_handlers;
};

/* Send a request to the device and wait for its result. */
BOOL ble_device_ioctl( struct ble_device *device, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size,
                       DWORD *ret_size );
HRESULT ble_device_get_info( struct ble_device *device, struct winebth_le_device_info *info );
HRESULT ble_device_connect( struct ble_device *device );
HRESULT ble_device_get_gatt_services( struct ble_device *device, const GUID *uuid,
                                      IVectorView_GattDeviceService **services );
void gatt_characteristics_value_changed( struct ble_device *device, UINT16 handle, const BYTE *data, UINT32 size );

HRESULT gatt_service_create( struct ble_device *device, const BTH_LE_GATT_SERVICE *svc, IGattDeviceService **service );
HRESULT gatt_services_result_create( GattCommunicationStatus status, IVectorView_GattDeviceService *services,
                                     IGattDeviceServicesResult **out );

HRESULT event_handlers_append( struct list *list, IUnknown *handler, EventRegistrationToken *token );
HRESULT event_handlers_remove( struct list *list, EventRegistrationToken *token );
void event_handlers_notify( struct list *list, IInspectable *sender, IInspectable *args );
void event_handlers_clear( struct list *list );

HRESULT buffer_create( const BYTE *data, UINT32 size, IBuffer **out );
HRESULT buffer_get_bytes( IBuffer *buffer, BYTE **data, UINT32 *size );
HRESULT reference_create( const GUID *iid, const void *value, UINT32 size, IInspectable **out );
HANDLE open_bluetooth_radio( DWORD flags );

#define DEFINE_IINSPECTABLE_STUBS( pfx, iface_type, class_name )                                   \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );                                  \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *name )            \
    {                                                                                              \
        TRACE( "(%p, %p)\n", iface, name );                                                        \
        return WindowsCreateString( class_name, wcslen( class_name ), name );                      \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *level )              \
    {                                                                                              \
        TRACE( "(%p, %p)\n", iface, level );                                                       \
        *level = BaseTrust;                                                                        \
        return S_OK;                                                                               \
    }

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )
#define DEFINE_IINSPECTABLE_OUTER( pfx, iface_type, impl_type, outer_iface )                       \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, impl->outer_iface )
#endif
