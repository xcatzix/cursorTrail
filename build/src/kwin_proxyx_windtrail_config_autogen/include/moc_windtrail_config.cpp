/****************************************************************************
** Meta object code from reading C++ file 'windtrail_config.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/windtrail_config.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'windtrail_config.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN4KWin15WindTrailConfigE_t {};
} // unnamed namespace

template <> constexpr inline auto KWin::WindTrailConfig::qt_create_metaobjectdata<qt_meta_tag_ZN4KWin15WindTrailConfigE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "KWin::WindTrailConfig",
        "chooseColor",
        "",
        "presetChanged",
        "index",
        "settingsChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'chooseColor'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'presetChanged'
        QtMocHelpers::SlotData<void(int)>(3, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Slot 'settingsChanged'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<WindTrailConfig, qt_meta_tag_ZN4KWin15WindTrailConfigE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject KWin::WindTrailConfig::staticMetaObject = { {
    QMetaObject::SuperData::link<KCModule::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4KWin15WindTrailConfigE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4KWin15WindTrailConfigE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN4KWin15WindTrailConfigE_t>.metaTypes,
    nullptr
} };

void KWin::WindTrailConfig::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WindTrailConfig *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->chooseColor(); break;
        case 1: _t->presetChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->settingsChanged(); break;
        default: ;
        }
    }
}

const QMetaObject *KWin::WindTrailConfig::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *KWin::WindTrailConfig::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN4KWin15WindTrailConfigE_t>.strings))
        return static_cast<void*>(this);
    return KCModule::qt_metacast(_clname);
}

int KWin::WindTrailConfig::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = KCModule::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
QT_WARNING_POP
