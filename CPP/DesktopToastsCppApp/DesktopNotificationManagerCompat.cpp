#include "DesktopNotificationManagerCompat.h"
#include <appmodel.h>
#include <string>

#define THROW_IF_FAILED(hr) if (FAILED(hr)) { throw hr; }

// Or could do throw std::exception("HRESULT", hr);

using namespace ABI::Windows::Data::Xml::Dom;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

bool DesktopNotificationManagerCompat::s_registeredAumidAndComServer = false;
const wchar_t *DesktopNotificationManagerCompat::s_aumid = nullptr;
bool DesktopNotificationManagerCompat::s_registeredActivator = false;
bool DesktopNotificationManagerCompat::s_hasCheckedIsRunningAsUwp = false;
bool DesktopNotificationManagerCompat::s_isRunningAsUwp = false;

void DesktopNotificationManagerCompat::RegisterAumidAndComServer(const wchar_t *aumid, GUID clsid)
{
    // If running as Desktop Bridge
    if (IsRunningAsUwp())
    {
        // Clear the AUMID since Desktop Bridge doesn't use it, and then we're done.
        // Desktop Bridge apps are registered with platform through their manifest.
        // Their LocalServer32 key is also registered through their manifest.
        s_aumid = nullptr;
        s_registeredAumidAndComServer = true;
        return;
    }

    s_aumid = aumid;

    // Get the EXE path
    wchar_t exePath[MAX_PATH];
    DWORD charWritten = ::GetModuleFileName(nullptr, exePath, ARRAYSIZE(exePath));
    THROW_IF_FAILED(charWritten > 0 ? S_OK : HRESULT_FROM_WIN32(::GetLastError()));

    // Register the COM server
    RegisterComServer(clsid, exePath);
    s_registeredAumidAndComServer = true;
}

void DesktopNotificationManagerCompat::RegisterActivator()
{
    // Module<OutOfProc> needs a callback registered before it can be used.
    // Since we don't care about when it shuts down, we'll pass an empty lambda here.
    Module<OutOfProc>::Create([] {});

    // If a local server process only hosts the COM object then COM expects
    // the COM server host to shutdown when the references drop to zero.
    // Since the user might still be using the program after activating the notification,
    // we don't want to shutdown immediately.  Incrementing the object count tells COM that
    // we aren't done yet.
    Module<OutOfProc>::GetModule().IncrementObjectCount();

    THROW_IF_FAILED(Module<OutOfProc>::GetModule().RegisterObjects());

    s_registeredActivator = true;
}

void DesktopNotificationManagerCompat::RegisterComServer(GUID clsid, wchar_t exePath[])
{
    // Turn the GUID into a string
    OLECHAR* clsidOlechar;
    StringFromCLSID(clsid, &clsidOlechar);
    std::wstring clsidStr(clsidOlechar);
    ::CoTaskMemFree(clsidOlechar);

    // Create the subkey
    // Something like SOFTWARE\Classes\CLSID\{23A5B06E-20BB-4E7E-A0AC-6982ED6A6041}\LocalServer32
    std::wstring subKeyStr = LR"(SOFTWARE\Classes\CLSID\)" + clsidStr + LR"(\LocalServer32)";
    LPCWSTR subKey = subKeyStr.c_str();

    // Include -ToastActivated launch args on the exe
    std::wstring exePathStr(exePath);
    exePathStr = L"\"" + exePathStr + L"\" -ToastActivated";
    exePath = new wchar_t[exePathStr.length() + 1];
    wcscpy(exePath, exePathStr.c_str());

    // We don't need to worry about overflow here as ::GetModuleFileName won't
    // return anything bigger than the max file system path (much fewer than max of DWORD).
    DWORD dataSize = static_cast<DWORD>((::wcslen(exePath) + 1) * sizeof(WCHAR));

    // Register the EXE for the COM server
    THROW_IF_FAILED(HRESULT_FROM_WIN32(::RegSetKeyValue(
        HKEY_CURRENT_USER,
        subKey,
        nullptr,
        REG_SZ,
        reinterpret_cast<const BYTE*>(exePath),
        dataSize)));
}

ComPtr<IToastNotifier> DesktopNotificationManagerCompat::CreateToastNotifier()
{
    EnsureRegistered();

    ComPtr<IToastNotificationManagerStatics> toastStatics;
    THROW_IF_FAILED(Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
        &toastStatics));

    ComPtr<IToastNotifier> notifier;
    if (s_aumid != nullptr)
    {
        THROW_IF_FAILED(toastStatics->CreateToastNotifierWithId(HStringReference(s_aumid).Get(), &notifier));
    }
    else
    {
        THROW_IF_FAILED(toastStatics->CreateToastNotifier(&notifier));
    }

    return notifier;
}

ComPtr<IToastNotification> DesktopNotificationManagerCompat::CreateToastNotification(IXmlDocument *content)
{
    ComPtr<IToastNotificationFactory> factory;
    THROW_IF_FAILED(Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
        &factory));

    ComPtr<IToastNotification> notification;
    THROW_IF_FAILED(factory->CreateToastNotification(content, &notification));

    return notification;
}

ComPtr<IXmlDocument> DesktopNotificationManagerCompat::CreateXmlDocumentFromString(const wchar_t *xmlString)
{
    ComPtr<IInspectable> docInspectable;
    THROW_IF_FAILED(RoActivateInstance(HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(), docInspectable.ReleaseAndGetAddressOf()));

    ComPtr<IXmlDocument> doc;
    THROW_IF_FAILED(docInspectable.As(&doc));

    ComPtr<IXmlDocumentIO> docIO;
    THROW_IF_FAILED(doc.As(&docIO));

    // Load the XML string
    THROW_IF_FAILED(docIO->LoadXml(HStringReference(xmlString).Get()));

    return doc;
}

std::unique_ptr<DesktopNotificationHistoryCompat> DesktopNotificationManagerCompat::get_History()
{
    EnsureRegistered();

    ComPtr<IToastNotificationManagerStatics> toastStatics;
    THROW_IF_FAILED(Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
        &toastStatics));

    ComPtr<IToastNotificationManagerStatics2> toastStatics2;
    THROW_IF_FAILED(toastStatics.As(&toastStatics2));

    ComPtr<IToastNotificationHistory> history;
    THROW_IF_FAILED(toastStatics2->get_History(&history));

    return std::unique_ptr<DesktopNotificationHistoryCompat>(new DesktopNotificationHistoryCompat(s_aumid, history));
}

bool DesktopNotificationManagerCompat::CanUseHttpImages()
{
    return IsRunningAsUwp();
}

void DesktopNotificationManagerCompat::EnsureRegistered()
{
    // If not registered AUMID yet
    if (!s_registeredAumidAndComServer)
    {
        // Check if Desktop Bridge
        if (IsRunningAsUwp())
        {
            // Implicitly registered, all good!
            s_registeredAumidAndComServer = true;
        }
        else
        {
            // Otherwise, incorrect usage
            throw std::exception("You must call RegisterAumidAndComServer first.");
        }
    }

    // If not registered activator yet
    if (!s_registeredActivator)
    {
        // Incorrect usage
        throw std::exception("You must call RegisterActivator first.");
    }
}

bool DesktopNotificationManagerCompat::IsRunningAsUwp()
{
    if (!s_hasCheckedIsRunningAsUwp)
    {
        s_hasCheckedIsRunningAsUwp = true;

        // https://stackoverflow.com/questions/39609643/determine-if-c-application-is-running-as-a-uwp-app-in-desktop-bridge-project
        UINT32 length;
        wchar_t packageFamilyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
        LONG result = GetPackageFamilyName(GetCurrentProcess(), &length, packageFamilyName);
        s_isRunningAsUwp = result == ERROR_SUCCESS;
    }

    return s_isRunningAsUwp;
}

DesktopNotificationHistoryCompat::DesktopNotificationHistoryCompat(const wchar_t *aumid, ComPtr<IToastNotificationHistory> history)
{
    m_aumid = aumid;
    m_history = history;
}

DesktopNotificationHistoryCompat::DesktopNotificationHistoryCompat()
{
    // TODO: This constructor should be removed, if we made this all support ComPtr this would probably not be necessary?
}

void DesktopNotificationHistoryCompat::Clear()
{
    if (m_aumid != nullptr)
    {
        THROW_IF_FAILED(m_history->ClearWithId(HStringReference(m_aumid).Get()));
    }
    else
    {
        THROW_IF_FAILED(m_history->Clear());
    }
}

ComPtr<ABI::Windows::Foundation::Collections::IVectorView<ToastNotification*>> DesktopNotificationHistoryCompat::GetHistory()
{
    ComPtr<IToastNotificationHistory> history(m_history);
    ComPtr<IToastNotificationHistory2> history2;
    THROW_IF_FAILED(history.As(&history2));

    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<ToastNotification*>> toasts;

    if (m_aumid != nullptr)
    {
        THROW_IF_FAILED(history2->GetHistoryWithId(HStringReference(m_aumid).Get(), &toasts));
    }
    else
    {
        THROW_IF_FAILED(history2->GetHistory(&toasts));
    }

    return toasts;
}

void DesktopNotificationHistoryCompat::Remove(const wchar_t *tag)
{
    if (m_aumid != nullptr)
    {
        THROW_IF_FAILED(m_history->RemoveGroupedTagWithId(HStringReference(tag).Get(), HStringReference(L"").Get(), HStringReference(m_aumid).Get()));
    }
    else
    {
        THROW_IF_FAILED(m_history->Remove(HStringReference(tag).Get()));
    }
}

void DesktopNotificationHistoryCompat::Remove(const wchar_t *tag, const wchar_t *group)
{
    if (m_aumid != nullptr)
    {
        THROW_IF_FAILED(m_history->RemoveGroupedTagWithId(HStringReference(tag).Get(), HStringReference(group).Get(), HStringReference(m_aumid).Get()));
    }
    else
    {
        THROW_IF_FAILED(m_history->RemoveGroupedTag(HStringReference(tag).Get(), HStringReference(group).Get()));
    }
}

void DesktopNotificationHistoryCompat::RemoveGroup(const wchar_t *group)
{
    if (m_aumid != nullptr)
    {
        THROW_IF_FAILED(m_history->RemoveGroupWithId(HStringReference(group).Get(), HStringReference(m_aumid).Get()));
    }
    else
    {
        THROW_IF_FAILED(m_history->RemoveGroup(HStringReference(group).Get()));
    }
}