{#-
 #  Jinja2 template for generating C common code for Legato APIs.
 #
 #  Note: C/C++ comments apply to the generated code.  For example this template itself is not
 #  autogenerated, but the comment is copied verbatim into the generated file when the template is
 #  expanded.
 #
 #  Copyright (C) Sierra Wireless Inc.
 #}
/*
 * ====================== WARNING ======================
 *
 * THE CONTENTS OF THIS FILE HAVE BEEN AUTO-GENERATED.
 * DO NOT MODIFY IN ANY WAY.
 *
 * ====================== WARNING ======================
 */
#ifndef {{apiBaseName|upper}}_COMMON_H_INCLUDE_GUARD
#define {{apiBaseName|upper}}_COMMON_H_INCLUDE_GUARD


#include "legato.h"
{%- if imports %}

// Interface specific includes
{%- for import in imports %}
#include "{{import}}_common.h"
{%- endfor %}
{%- endif %}


{% for define in definitions %}

//--------------------------------------------------------------------------------------------------
{{define.comment|FormatHeaderComment}}
//--------------------------------------------------------------------------------------------------
{%- if define.value is string %}
#define {{apiBaseName|upper}}_{{define.name}} "{{define.value|EscapeString}}"
{%- else %}
#define {{apiBaseName|upper}}_{{define.name}} {{define.value}}
{%- endif %}
{%- endfor %}
{%- for type in types if type is not HandlerType %}

//--------------------------------------------------------------------------------------------------
{{type.comment|FormatHeaderComment}}
//--------------------------------------------------------------------------------------------------
{%- if type is EnumType %}
typedef enum
{
    {%- for element in type.elements %}
    {{apiBaseName|upper}}_{{element.name}} = {{element.value}}{%if not loop.last%},{%endif%}
        ///<{{element.comments|join("\n///<")|indent(8)}}
    {%- endfor %}
}
{{type|FormatType(useBaseName=True)}};
{%- elif type is BitMaskType %}
typedef enum
{
    {%- for element in type.elements %}
    {{apiBaseName|upper}}_{{element.name}} = {{"0x%x" % element.value}}{%if not loop.last%},{%endif%}
    {%- if element.comments %}        ///<{{element.comments|join("\n///<")|indent(8)}}{%endif%}
    {%- endfor %}
}
{{type|FormatType(useBaseName=True)}};
{%- elif type is StructType %}
typedef struct
{
    {%- for member in type.members %}
    {%- if member is StringMember %}
    char {{member.name|DecorateName}}[{{member.maxCount}} + 1];
    {%- else %}
    {{member.apiType|FormatType(useBaseName=True)}} {{member.name|DecorateName}}
    {%- if member is ArrayMember %}[{{member.maxCount}}]{% endif %};
    {%- endif %}
    {%- endfor %}
}
{{type|FormatType(useBaseName=True)}};
{%- elif type is ReferenceType %}
typedef struct {{apiBaseName}}_{{type.name}}* {{type|FormatType(useBaseName=True)}};
{%- endif %}
{% endfor %}
{%- for handler in types if handler is HandlerType %}

//--------------------------------------------------------------------------------------------------
{{handler.comment|FormatHeaderComment}}
//--------------------------------------------------------------------------------------------------
typedef void (*{{handler|FormatType(useBaseName=True)}})
(
    {%- for parameter in handler|CAPIParameters %}
        {{parameter|FormatParameter(useBaseName=True)}}{% if not loop.last %},{% endif %}
        ///<{{parameter.comments|join("\n///<")|indent(8)}}
    {%-endfor%}
);
{%- endfor %}


//--------------------------------------------------------------------------------------------------
/**
 * Get if this client bound locally.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED bool ifgen_{{apiBaseName}}_HasLocalBinding
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * Init data that is common across all threads
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void ifgen_{{apiBaseName}}_InitCommonData
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * Perform common initialization and open a session
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED le_result_t ifgen_{{apiBaseName}}_OpenSession
(
    le_msg_SessionRef_t _ifgen_sessionRef,
    bool isBlocking
);

{%- for function in functions %}

//--------------------------------------------------------------------------------------------------
{{function.comment|FormatHeaderComment}}
//--------------------------------------------------------------------------------------------------
LE_SHARED {{function.returnType|FormatType(useBaseName=True)}} ifgen_{{apiBaseName}}_{{function.name}}
(
    le_msg_SessionRef_t _ifgen_sessionRef
    {%- for parameter in function|CAPIParameters %}
    {%- if loop.first %},{% endif %}
        {{parameter|FormatParameter(useBaseName=True)}}{% if not loop.last %},{% endif %}
        ///< [{{parameter.direction|FormatDirection}}]
             {{-parameter.comments|join("\n///<")|indent(8)}}
    {%-endfor%}
);
{%- endfor %}

#endif // {{apiBaseName|upper}}_COMMON_H_INCLUDE_GUARD
