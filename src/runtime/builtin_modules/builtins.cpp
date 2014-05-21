// Copyright (c) 2014 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>

#include "core/ast.h"
#include "core/types.h"

#include "codegen/compvars.h"

#include "runtime/gc_runtime.h"
#include "runtime/objmodel.h"
#include "runtime/set.h"
#include "runtime/types.h"
#include "runtime/util.h"

#include "runtime/inline/xrange.h"

#include "gc/collector.h"

namespace pyston {

extern "C" Box* trap() {
    raise(SIGTRAP);

    return None;
}

extern "C" Box* abs_(Box* x) {
    if (x->cls == int_cls) {
        i64 n = static_cast<BoxedInt*>(x)->n;
        return boxInt(n >= 0 ? n : -n);
    } else if (x->cls == float_cls) {
        double d = static_cast<BoxedFloat*>(x)->d;
        return boxFloat(d >= 0 ? d : -d);
    } else {
        RELEASE_ASSERT(0, "%s", getTypeName(x)->c_str());
    }
}

extern "C" Box* all(Box* container) {
    for (Box* e : container->pyElements()) {
        if (!nonzero(e)) {
            return boxBool(false);
        }
    }
    return boxBool(true);
}

extern "C" Box* any(Box* container) {
    for (Box* e : container->pyElements()) {
        if (nonzero(e)) {
            return boxBool(true);
        }
    }
    return boxBool(false);
}

extern "C" Box* min1(Box* container) {
    Box* minElement = nullptr;
    for (Box* e : container->pyElements()) {
        if (!minElement) {
            minElement = e;
        } else {
            Box* comp_result = compareInternal(minElement, e, AST_TYPE::Gt, NULL);
            if (nonzero(comp_result)) {
                minElement = e;
            }
        }
    }

    if (!minElement) {
        raiseExcHelper(ValueError, "min() arg is an empty sequence");
    }
    return minElement;
}

extern "C" Box* min2(Box* o0, Box* o1) {
    Box* comp_result = compareInternal(o0, o1, AST_TYPE::Gt, NULL);
    bool b = nonzero(comp_result);
    if (b) {
        return o1;
    }
    return o0;
}

extern "C" Box* max1(Box* container) {
    Box* maxElement = nullptr;
    for (Box* e : container->pyElements()) {
        if (!maxElement) {
            maxElement = e;
        } else {
            Box* comp_result = compareInternal(maxElement, e, AST_TYPE::Lt, NULL);
            if (nonzero(comp_result)) {
                maxElement = e;
            }
        }
    }

    if (!maxElement) {
        raiseExcHelper(ValueError, "max() arg is an empty sequence");
    }
    return maxElement;
}

extern "C" Box* max2(Box* o0, Box* o1) {
    Box* comp_result = compareInternal(o0, o1, AST_TYPE::Lt, NULL);
    bool b = nonzero(comp_result);
    if (b) {
        return o1;
    }
    return o0;
}

extern "C" Box* open2(Box* arg1, Box* arg2) {
    if (arg1->cls != str_cls) {
        fprintf(stderr, "TypeError: coercing to Unicode: need string of buffer, %s found\n",
                getTypeName(arg1)->c_str());
        raiseExcHelper(TypeError, "");
    }
    if (arg2->cls != str_cls) {
        fprintf(stderr, "TypeError: coercing to Unicode: need string of buffer, %s found\n",
                getTypeName(arg2)->c_str());
        raiseExcHelper(TypeError, "");
    }

    const std::string& fn = static_cast<BoxedString*>(arg1)->s;
    const std::string& mode = static_cast<BoxedString*>(arg2)->s;

    FILE* f = fopen(fn.c_str(), mode.c_str());
    RELEASE_ASSERT(f, "");

    return new BoxedFile(f);
}

extern "C" Box* open1(Box* arg) {
    Box* mode = boxStrConstant("r");
    Box* rtn = open2(arg, mode);
    return rtn;
}

extern "C" Box* chr(Box* arg) {
    if (arg->cls != int_cls) {
        fprintf(stderr, "TypeError: coercing to Unicode: need string of buffer, %s found\n", getTypeName(arg)->c_str());
        raiseExcHelper(TypeError, "");
    }
    i64 n = static_cast<BoxedInt*>(arg)->n;
    RELEASE_ASSERT(n >= 0 && n < 256, "");

    return boxString(std::string(1, (char)n));
}

extern "C" Box* ord(Box* arg) {
    if (arg->cls != str_cls) {
        raiseExcHelper(TypeError, "ord() expected string of length 1, but %s found", getTypeName(arg)->c_str());
    }
    const std::string& s = static_cast<BoxedString*>(arg)->s;

    if (s.size() != 1)
        raiseExcHelper(TypeError, "ord() expected string of length 1, but string of length %d found", s.size());

    return boxInt(s[0]);
}

Box* range1(Box* end) {
    RELEASE_ASSERT(end->cls == int_cls, "%s", getTypeName(end)->c_str());

    BoxedList* rtn = new BoxedList();
    i64 iend = static_cast<BoxedInt*>(end)->n;
    rtn->ensure(iend);
    for (i64 i = 0; i < iend; i++) {
        Box* bi = boxInt(i);
        listAppendInternal(rtn, bi);
    }
    return rtn;
}

Box* range2(Box* start, Box* end) {
    RELEASE_ASSERT(start->cls == int_cls, "%s", getTypeName(start)->c_str());
    RELEASE_ASSERT(end->cls == int_cls, "%s", getTypeName(end)->c_str());

    BoxedList* rtn = new BoxedList();
    i64 istart = static_cast<BoxedInt*>(start)->n;
    i64 iend = static_cast<BoxedInt*>(end)->n;
    if ((iend - istart) > 0)
        rtn->ensure(iend - istart);
    for (i64 i = istart; i < iend; i++) {
        listAppendInternal(rtn, boxInt(i));
    }
    return rtn;
}

Box* range3(Box* start, Box* end, Box* step) {
    RELEASE_ASSERT(start->cls == int_cls, "%s", getTypeName(start)->c_str());
    RELEASE_ASSERT(end->cls == int_cls, "%s", getTypeName(end)->c_str());
    RELEASE_ASSERT(step->cls == int_cls, "%s", getTypeName(step)->c_str());

    BoxedList* rtn = new BoxedList();
    i64 istart = static_cast<BoxedInt*>(start)->n;
    i64 iend = static_cast<BoxedInt*>(end)->n;
    i64 istep = static_cast<BoxedInt*>(step)->n;
    RELEASE_ASSERT(istep != 0, "step can't be 0");

    if (istep > 0) {
        for (i64 i = istart; i < iend; i += istep) {
            Box* bi = boxInt(i);
            listAppendInternal(rtn, bi);
        }
    } else {
        for (i64 i = istart; i > iend; i += istep) {
            Box* bi = boxInt(i);
            listAppendInternal(rtn, bi);
        }
    }
    return rtn;
}

Box* notimplementedRepr(Box* self) {
    assert(self == NotImplemented);
    return boxStrConstant("NotImplemented");
}

Box* sorted(Box* obj) {
    RELEASE_ASSERT(obj->cls == list_cls, "");

    BoxedList* lobj = static_cast<BoxedList*>(obj);
    BoxedList* rtn = new BoxedList();

    int size = lobj->size;
    rtn->elts = new (size) BoxedList::ElementArray();
    rtn->size = size;
    rtn->capacity = size;
    for (int i = 0; i < size; i++) {
        Box* t = rtn->elts->elts[i] = lobj->elts->elts[i];
    }

    std::sort<Box**, PyLt>(rtn->elts->elts, rtn->elts->elts + size, PyLt());

    return rtn;
}

Box* isinstance_func(Box* obj, Box* cls) {
    assert(cls->cls == type_cls);
    BoxedClass* ccls = static_cast<BoxedClass*>(cls);

    return boxBool(isinstance(obj, cls, 0));
}

Box* getattr2(Box* obj, Box* _str) {
    if (_str->cls != str_cls) {
        raiseExcHelper(TypeError, "getattr(): attribute name must be string");
    }

    BoxedString* str = static_cast<BoxedString*>(_str);
    Box* rtn = getattr_internal(obj, str->s, true, true, NULL, NULL);

    if (!rtn) {
        raiseExcHelper(AttributeError, "'%s' object has no attribute '%s'", getTypeName(obj)->c_str(), str->s.c_str());
    }

    return rtn;
}

Box* getattr3(Box* obj, Box* _str, Box* default_value) {
    if (_str->cls != str_cls) {
        raiseExcHelper(TypeError, "getattr(): attribute name must be string");
    }

    BoxedString* str = static_cast<BoxedString*>(_str);
    Box* rtn = getattr_internal(obj, str->s, true, true, NULL, NULL);

    if (!rtn) {
        return default_value;
    }

    return rtn;
}

Box* map2(Box* f, Box* container) {
    Box* rtn = new BoxedList();
    for (Box* e : container->pyElements()) {
        listAppendInternal(rtn, runtimeCall(f, 1, e, NULL, NULL, NULL));
    }
    return rtn;
}

extern "C" const ObjectFlavor notimplemented_flavor(&boxGCHandler, NULL);
BoxedClass* notimplemented_cls;
BoxedModule* builtins_module;

// TODO looks like CPython and pypy put this into an "exceptions" module:
BoxedClass* Exception, *AssertionError, *AttributeError, *TypeError, *NameError, *KeyError, *IndexError, *IOError,
    *OSError, *ZeroDivisionError, *ValueError, *UnboundLocalError, *RuntimeError, *ImportError;

const ObjectFlavor exception_flavor(&boxGCHandler, NULL);
Box* exceptionNew1(BoxedClass* cls) {
    return exceptionNew2(cls, boxStrConstant(""));
}

Box* exceptionNew2(BoxedClass* cls, Box* message) {
    HCBox* r = new HCBox(&exception_flavor, cls);
    r->giveAttr("message", message);
    return r;
}

Box* exceptionStr(Box* b) {
    HCBox* hcb = static_cast<HCBox*>(b);

    // TODO In CPython __str__ and __repr__ pull from an internalized message field, but for now do this:
    Box* message = hcb->peekattr("message");
    assert(message);
    message = str(message);
    assert(message->cls == str_cls);

    return message;
}

Box* exceptionRepr(Box* b) {
    HCBox* hcb = static_cast<HCBox*>(b);

    // TODO In CPython __str__ and __repr__ pull from an internalized message field, but for now do this:
    Box* message = hcb->peekattr("message");
    assert(message);
    message = repr(message);
    assert(message->cls == str_cls);

    BoxedString* message_s = static_cast<BoxedString*>(message);
    return boxString(*getTypeName(b) + "(" + message_s->s + ",)");
}

static BoxedClass* makeBuiltinException(const char* name) {
    BoxedClass* cls = new BoxedClass(true, false);
    cls->giveAttr("__name__", boxStrConstant(name));

    // TODO these should be on the base Exception class:
    cls->giveAttr("__new__", new BoxedFunction(boxRTFunction((void*)exceptionNew1, NULL, 1, false)));
    cls->giveAttr("__str__", new BoxedFunction(boxRTFunction((void*)exceptionStr, NULL, 1, false)));
    cls->giveAttr("__repr__", new BoxedFunction(boxRTFunction((void*)exceptionRepr, NULL, 1, false)));
    cls->freeze();

    builtins_module->giveAttr(name, cls);
    return cls;
}

void setupBuiltins() {
    builtins_module = createModule("__builtin__", "__builtin__");

    builtins_module->setattr("None", None, NULL, NULL);

    notimplemented_cls = new BoxedClass(false, false);
    notimplemented_cls->giveAttr("__name__", boxStrConstant("NotImplementedType"));
    notimplemented_cls->giveAttr("__repr__",
                                 new BoxedFunction(boxRTFunction((void*)notimplementedRepr, NULL, 1, false)));
    notimplemented_cls->freeze();
    NotImplemented = new Box(&notimplemented_flavor, notimplemented_cls);
    gc::registerStaticRootObj(NotImplemented);

    builtins_module->giveAttr("NotImplemented", NotImplemented);
    builtins_module->giveAttr("NotImplementedType", notimplemented_cls);

    builtins_module->giveAttr("all", new BoxedFunction(boxRTFunction((void*)all, BOXED_BOOL, 1, false)));
    builtins_module->giveAttr("any", new BoxedFunction(boxRTFunction((void*)any, BOXED_BOOL, 1, false)));

    Exception = makeBuiltinException("Exception");
    AssertionError = makeBuiltinException("AssertionError");
    AttributeError = makeBuiltinException("AttributeError");
    TypeError = makeBuiltinException("TypeError");
    NameError = makeBuiltinException("NameError");
    KeyError = makeBuiltinException("KeyError");
    IndexError = makeBuiltinException("IndexError");
    IOError = makeBuiltinException("IOError");
    OSError = makeBuiltinException("OSError");
    ZeroDivisionError = makeBuiltinException("ZeroDivisionError");
    ValueError = makeBuiltinException("ValueError");
    UnboundLocalError = makeBuiltinException("UnboundLocalError");
    RuntimeError = makeBuiltinException("RuntimeError");
    ImportError = makeBuiltinException("ImportError");

    repr_obj = new BoxedFunction(boxRTFunction((void*)repr, NULL, 1, false));
    builtins_module->giveAttr("repr", repr_obj);
    len_obj = new BoxedFunction(boxRTFunction((void*)len, NULL, 1, false));
    builtins_module->giveAttr("len", len_obj);
    hash_obj = new BoxedFunction(boxRTFunction((void*)hash, NULL, 1, false));
    builtins_module->giveAttr("hash", hash_obj);
    abs_obj = new BoxedFunction(boxRTFunction((void*)abs_, NULL, 1, false));
    builtins_module->giveAttr("abs", abs_obj);

    CLFunction* min_func = boxRTFunction((void*)min1, NULL, 1, false);
    addRTFunction(min_func, (void*)min2, NULL, 2, false);
    min_obj = new BoxedFunction(min_func);
    builtins_module->giveAttr("min", min_obj);

    CLFunction* max_func = boxRTFunction((void*)max1, NULL, 1, false);
    addRTFunction(max_func, (void*)max2, NULL, 2, false);
    max_obj = new BoxedFunction(max_func);
    builtins_module->giveAttr("max", max_obj);

    chr_obj = new BoxedFunction(boxRTFunction((void*)chr, NULL, 1, false));
    builtins_module->giveAttr("chr", chr_obj);
    ord_obj = new BoxedFunction(boxRTFunction((void*)ord, NULL, 1, false));
    builtins_module->giveAttr("ord", ord_obj);
    trap_obj = new BoxedFunction(boxRTFunction((void*)trap, NULL, 0, false));
    builtins_module->giveAttr("trap", trap_obj);

    CLFunction* getattr_func = boxRTFunction((void*)getattr2, NULL, 2, false);
    addRTFunction(getattr_func, (void*)getattr3, NULL, 3, false);
    builtins_module->giveAttr("getattr", new BoxedFunction(getattr_func));

    Box* isinstance_obj = new BoxedFunction(boxRTFunction((void*)isinstance_func, NULL, 2, false));
    builtins_module->giveAttr("isinstance", isinstance_obj);

    builtins_module->giveAttr("sorted", new BoxedFunction(boxRTFunction((void*)sorted, NULL, 1, false)));

    builtins_module->setattr("True", True, NULL, NULL);
    builtins_module->setattr("False", False, NULL, NULL);

    CLFunction* range_clf = boxRTFunction((void*)range1, NULL, 1, false);
    addRTFunction(range_clf, (void*)range2, NULL, 2, false);
    addRTFunction(range_clf, (void*)range3, NULL, 3, false);
    range_obj = new BoxedFunction(range_clf);
    builtins_module->giveAttr("range", range_obj);

    setupXrange();
    builtins_module->giveAttr("xrange", xrange_cls);

    CLFunction* open = boxRTFunction((void*)open1, NULL, 1, false);
    addRTFunction(open, (void*)open2, NULL, 2, false);
    open_obj = new BoxedFunction(open);
    builtins_module->giveAttr("open", open_obj);

    builtins_module->giveAttr("map", new BoxedFunction(boxRTFunction((void*)map2, LIST, 2, false)));

    builtins_module->setattr("str", str_cls, NULL, NULL);
    builtins_module->setattr("int", int_cls, NULL, NULL);
    builtins_module->setattr("float", float_cls, NULL, NULL);
    builtins_module->setattr("list", list_cls, NULL, NULL);
    builtins_module->setattr("slice", slice_cls, NULL, NULL);
    builtins_module->setattr("type", type_cls, NULL, NULL);
    builtins_module->setattr("file", file_cls, NULL, NULL);
    builtins_module->setattr("bool", bool_cls, NULL, NULL);
    builtins_module->setattr("dict", dict_cls, NULL, NULL);
    builtins_module->setattr("set", set_cls, NULL, NULL);
    builtins_module->setattr("tuple", tuple_cls, NULL, NULL);
    builtins_module->setattr("instancemethod", instancemethod_cls, NULL, NULL);
}
}
