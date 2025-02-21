//
// Created by Perfare on 2020/7/4.
//

#ifdef _WIN32
#include <windows.h>
#endif

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

struct il2cppString : Il2CppObject // Credits: il2cpp resolver (https://github.com/sneakyevil/IL2CPP_Resolver/blob/main/Unity/Structures/System_String.hpp)
{
    int m_iLength;
    wchar_t m_wString[1024];

    std::string ToString()
    {
    #ifdef _WIN32
        std::string sRet(static_cast<size_t>(m_iLength) * 3 + 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, m_wString, m_iLength, &sRet[0], static_cast<int>(sRet.size()), 0, 0);
        return sRet;
    #else
        // On Android/Linux, use wcstombs for conversion
        size_t length = wcstombs(nullptr, m_wString, 0);
        if (length == (size_t)-1) {
            return "[Invalid UTF-16]";
        }

        std::string sRet(length, '\0');
        wcstombs(&sRet[0], m_wString, length);
        return sRet;
    #endif
}


};


#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) {                                   \
        LOGW("api not found %s", #n);          \
    }                                          \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

std::string GetFullType(const Il2CppType* type) {
    if (!type) {
        LOGE("[ERROR] Null Il2CppType pointer passed to GetFullType.");
        return "UnknownType";
    }

    // If the type is generic, handle it explicitly
    if (type->type == IL2CPP_TYPE_GENERICINST) {
        Il2CppGenericClass* genericClass = type->data.generic_class;

        if (!genericClass || !genericClass->cached_class) {
            LOGE("[ERROR] Null generic class or cached class in generic type.");
            return "InvalidGenericType";
        }

        std::string result = il2cpp_class_get_name(genericClass->cached_class);
        if (result.empty()) {
            LOGE("[ERROR] Failed to get class name for generic type.");
            return "UnnamedGenericType";
        }

        size_t backtickPos = result.find('`');
        if (backtickPos != std::string::npos) {
            result = result.substr(0, backtickPos);
        }

        result += "<";
        const Il2CppGenericInst* classInst = genericClass->context.class_inst;
        if (classInst) {
            for (uint32_t i = 0; i < classInst->type_argc; ++i) {
                const Il2CppType* argType = classInst->type_argv[i];
                result += argType ? GetFullType(argType) : "UnknownType";

                if (i < classInst->type_argc - 1) {
                    result += ", ";
                }
            }
        }
        else {
            LOGE("[ERROR] Null class instance for generic type.");
            result += "UnknownType";
        }

        result += ">";
        return result;
    }

    // Fallback for non-generic types
    Il2CppClass* typeClass = il2cpp_class_from_type(type);
    if (typeClass) {
        const char* typeName = il2cpp_type_get_name(type);
        return typeName ? std::string(typeName) : "Unknown";
    }

    LOGE("[ERROR] Failed to resolve class from Il2CppType.");
    return "UnknownType";
}


std::string Field_ReturnType(FieldInfo* field)
{
    auto field_type = il2cpp_field_get_type(field);
    auto field_class = il2cpp_class_from_type(field_type);
    return std::string(il2cpp_class_get_name(field_class));
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string dump_method(Il2CppClass* klass) {
    if (!klass) {
        return "\t// Invalid class pointer\n";
    }

    std::stringstream outPut;
    outPut << "\n\t// Methods\n\n";
    void* iter = nullptr;

    while (true) {
        auto method = il2cpp_class_get_methods(klass, &iter);
        if (!method) {
            break;  // No more methods to process
        }

        if (!method->methodPointer) {
            outPut << "\t// RVA: 0x VA: 0x0\n";
        } else {
            outPut << "\t// RVA: 0x" << std::hex 
                   << (uint64_t)method->methodPointer - il2cpp_base 
                   << " VA: 0x" << (uint64_t)method->methodPointer << "\n";
        }

        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        outPut << get_method_modifier(flags);

        auto return_type = il2cpp_method_get_return_type(method);
        if (!return_type) {
            LOGE("Failed to get return type for method: %s", il2cpp_method_get_name(method));
            outPut << "/* Unknown Return Type */ ";
            continue;
        }

        if (_il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }

        outPut << GetFullType(return_type) << " " << il2cpp_method_get_name(method) << "(";

        auto param_count = il2cpp_method_get_param_count(method);
        for (int i = 0; i < (int)param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            if (!param) {
                LOGE("Failed to get parameter %d for method: %s", i, il2cpp_method_get_name(method));
                continue;
            }

            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }

            auto parameter_class = il2cpp_class_from_type(param);
            if (!parameter_class) {
                LOGE("Failed to resolve parameter class for method: %s", il2cpp_method_get_name(method));
                outPut << "UnknownType";
            } else {
                if (param->type == IL2CPP_TYPE_GENERICINST) {
                    outPut << GetFullType(param);
                } else {
                    outPut << il2cpp_class_get_name(parameter_class);
                }
            }

            outPut << " " << il2cpp_method_get_param_name(method, i);
            if (i < (int)param_count - 1) {
                outPut << ", ";
            }
        }

        outPut << ") { }\n\n";
    }

    return outPut.str();
}


std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        //TODO attribute
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method(prop);
        auto set = il2cpp_property_get_set_method(prop);
        auto prop_name = il2cpp_property_get_name(prop);
        outPut << "\t";
        Il2CppClass *prop_class = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            prop_class = il2cpp_class_from_type(il2cpp_method_get_return_type(get));
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            auto param = il2cpp_method_get_param(set, 0);
            prop_class = il2cpp_class_from_type(param);
        }
        if (prop_class) {
            outPut << il2cpp_class_get_name(prop_class) << " " << prop_name << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name;
            }
        }
    }
    return outPut.str();
}

#define INIT_CONST_FIELD(type) \
        outPut << " = ";        \
        type data;               \
		il2cpp_field_static_get_value(field, &data)

#define FieldIs(typeName) FieldType == typeName

#define INIT_CONST_NUMBER_FIELD(type, typeName, stdtype) \
        if (FieldIs(typeName)) {                          \
            INIT_CONST_FIELD(type);                        \
            outPut << stdtype << data;                      \
        }

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    auto is_enum = il2cpp_class_is_enum(klass);
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type(field);
        auto field_class = il2cpp_class_from_type(field_type);
        outPut << GetFullType(field_type) << " " << il2cpp_field_get_name(field);
        if (attrs & FIELD_ATTRIBUTE_LITERAL && is_enum) {
            uint64_t val = 0;
            il2cpp_field_static_get_value(field, &val);
            outPut << " = " << std::dec << val;
        }

        if (il2cpp_field_is_literal(field)) { // field_is_const
            std::string FieldType = Field_ReturnType(field);

            if (FieldIs("String")) {
                INIT_CONST_FIELD(il2cppString*);
                if (data != nullptr) {
                    std::string skibidi = data->ToString().c_str();
                    if (skibidi.size() == 1) {
                        outPut << "'" << skibidi.c_str() << "'";
                    }
                    else {
                        outPut << "\"" << skibidi.c_str() << "\"";
                    }
                }
            }

            if (FieldIs("Boolean")) {
                INIT_CONST_FIELD(bool);
                if (data) outPut << "true";
                else outPut << "false";
            }

            INIT_CONST_NUMBER_FIELD(int16_t, "Int16", std::dec)
            INIT_CONST_NUMBER_FIELD(int, "Int32", std::dec)
            INIT_CONST_NUMBER_FIELD(int64_t, "Int64", std::dec)

            INIT_CONST_NUMBER_FIELD(double, "Double", std::showpoint)
            INIT_CONST_NUMBER_FIELD(float, "Single", std::showpoint)

            INIT_CONST_NUMBER_FIELD(int16_t, "UInt16", std::dec)
            INIT_CONST_NUMBER_FIELD(uint32_t, "UInt32", std::dec)
            INIT_CONST_NUMBER_FIELD(int64_t, "UInt64", std::dec)
        }

        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    if (!type) {
        return "// Invalid type pointer\n";
    }

    std::stringstream outPut;

    auto *klass = il2cpp_class_from_type(type);

    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";

    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    //TODO attribute
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << il2cpp_class_get_name(klass); //TODO genericContainerIndex
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_class_get_name(parent));
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        extends.emplace_back(il2cpp_class_get_name(itf));
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (int i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    //TODO EventInfo
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);
    if (il2cpp_domain_get_assemblies) {
        Dl_info dlInfo;
        if (dladdr((void *) il2cpp_domain_get_assemblies, &dlInfo)) {
            il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
        }
        LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);
    } else {
        LOGE("Failed to initialize il2cpp api.");
        return;
    }
    while (!il2cpp_is_vm_thread(nullptr)) {
        LOGI("Waiting for il2cpp_init...");
        sleep(1);
    }
    auto domain = il2cpp_domain_get();
    il2cpp_thread_attach(domain);
}

void il2cpp_dump(const char *outDir) {
    LOGI("Starting dump...");
    auto StartTimer = std::chrono::high_resolution_clock::now();

    size_t size;
    auto domain = il2cpp_domain_get();
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    std::stringstream imageOutput;
    std::stringstream imageList;
    std::vector<std::string> outPuts;

    // Dynamically generate the image list at the top
    for (size_t i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        const char* imageName = il2cpp_image_get_name(image);
        uintptr_t imageAddress = reinterpret_cast<uintptr_t>(image);

        imageList << "// Image " << i << ": " << imageName << "\n";
    }

    if (il2cpp_image_get_class) {
        LOGI("Version greater than 2018.3");
        //使用il2cpp_image_get_class
        for (int i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            std::stringstream imageStr;
            imageStr << "\n// Dll : " << il2cpp_image_get_name(image);
            auto classCount = il2cpp_image_get_class_count(image);

            for (int j = 0; j < classCount; ++j) {
                Il2CppClass* klass = const_cast<Il2CppClass*>(il2cpp_image_get_class(image, j));
                if (!klass) {
                    LOGD("Class at index %zu is null.", (size_t)j);
                    continue;
                }

                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                LOGD("type name : %s", il2cpp_type_get_name(type));
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);

            }
        }
    } else {
        LOGI("Version less than 2018.3");
        //使用反射
        auto corlib = il2cpp_get_corlib();
        auto assemblyClass = il2cpp_class_from_name(corlib, "System.Reflection", "Assembly");
        auto assemblyLoad = il2cpp_class_get_method_from_name(assemblyClass, "Load", 1);
        auto assemblyGetTypes = il2cpp_class_get_method_from_name(assemblyClass, "GetTypes", 0);
        if (assemblyLoad && assemblyLoad->methodPointer) {
            LOGI("Assembly::Load: %p", assemblyLoad->methodPointer);
        } else {
            LOGI("miss Assembly::Load");
            return;
        }
        if (assemblyGetTypes && assemblyGetTypes->methodPointer) {
            LOGI("Assembly::GetTypes: %p", assemblyGetTypes->methodPointer);
        } else {
            LOGI("miss Assembly::GetTypes");
            return;
        }
        typedef void *(*Assembly_Load_ftn)(void *, Il2CppString *, void *);
        typedef Il2CppArray *(*Assembly_GetTypes_ftn)(void *, void *);
        for (int i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            std::stringstream imageStr;
            auto image_name = il2cpp_image_get_name(image);
            imageStr << "\n// Dll : " << image_name;
            //LOGD("image name : %s", image->name);
            auto imageName = std::string(image_name);
            auto pos = imageName.rfind('.');
            auto imageNameNoExt = imageName.substr(0, pos);
            auto assemblyFileName = il2cpp_string_new(imageNameNoExt.data());
            auto reflectionAssembly = ((Assembly_Load_ftn) assemblyLoad->methodPointer)(nullptr,
                                                                                        assemblyFileName,
                                                                                        nullptr);
            auto reflectionTypes = ((Assembly_GetTypes_ftn) assemblyGetTypes->methodPointer)(
                    reflectionAssembly, nullptr);
            auto items = reflectionTypes->vector;
            for (int j = 0; j < reflectionTypes->max_length; ++j) {
                auto klass = il2cpp_class_from_system_type((Il2CppReflectionType *) items[j]);
                auto type = il2cpp_class_get_type(klass);
                //LOGD("type name : %s", il2cpp_type_get_name(type));
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);
            }
        }
    }
    LOGI("write dump file");

    auto outPath = std::string(outDir).append("/files/dump.cs");
    std::ofstream outStream(outPath);

    outStream << imageList.str() << "\n";
    
    outStream << imageOutput.str();
    auto count = outPuts.size();
    for (int i = 0; i < count; ++i) {
        outStream << outPuts[i];
    }
    outStream.close();
    
    auto EndTimer = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> Timer = EndTimer - StartTimer;
    LOGI("Dumping complete! Took %f seconds.", Timer.count());
}
