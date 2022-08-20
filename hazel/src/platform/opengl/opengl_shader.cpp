#include "platform/opengl/opengl_shader.h"

#include <glad/glad.h>

#include <fstream>
#include <glm/gtc/type_ptr.hpp>

#include "hazel/core/log.h"
#include "hzpch.h"

namespace hazel {

static unsigned int ShaderTypeFromString(const std::string& type) {
  if (type == "vertex") return GL_VERTEX_SHADER;
  if (type == "fragment" || type == "pixel") return GL_FRAGMENT_SHADER;

  HZ_CORE_ASSERT(false, "Unknown shader type!");
  return 0;
}

OpenGLShader::OpenGLShader(const std::string& filepath) {
  std::string source        = ReadFile(filepath);
  auto        shaderSources = PreProcess(source);
  Compile(shaderSources);

  // Extract name from file path
  auto lastSlash = filepath.find_last_of("/\\");
  lastSlash      = lastSlash == std::string::npos ? 0 : lastSlash + 1;
  auto lastDot   = filepath.rfind('.');
  auto count     = lastDot == std::string::npos ? filepath.size() - lastSlash
                                                : lastDot - lastSlash;
  name_          = filepath.substr(lastSlash, count);
}

OpenGLShader::OpenGLShader(const std::string& name,
                           const std::string& vertexSrc,
                           const std::string& fragmentSrc)
    : name_(name) {
  std::unordered_map<unsigned int, std::string> sources;
  sources[GL_VERTEX_SHADER]   = vertexSrc;
  sources[GL_FRAGMENT_SHADER] = fragmentSrc;
  Compile(sources);

  // Create an empty vertex shader handle
  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);

  // Send the vertex shader source code to GL
  // Note that std::string's .c_str is NULL character terminated.
  const char* source = vertexSrc.c_str();
  glShaderSource(vertexShader, 1, &source, 0);

  // Compile the vertex shader
  glCompileShader(vertexShader);

  int isCompiled = 0;
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
  if (isCompiled == GL_FALSE) {
    int maxLength = 0;
    glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    std::vector<char> infoLog(maxLength);
    glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &infoLog[0]);

    // We don't need the shader anymore.
    glDeleteShader(vertexShader);

    HZ_CORE_ERROR("{0}", infoLog.data());
    HZ_CORE_ASSERT(false, "Vertex shader compilation failure!");
    return;
  }

  // Create an empty fragment shader handle
  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

  // Send the fragment shader source code to GL
  // Note that std::string's .c_str is NULL character terminated.
  source = fragmentSrc.c_str();
  glShaderSource(fragmentShader, 1, &source, 0);

  // Compile the fragment shader
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
  if (isCompiled == GL_FALSE) {
    int maxLength = 0;
    glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    std::vector<char> infoLog(maxLength);
    glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);

    // We don't need the shader anymore.
    glDeleteShader(fragmentShader);
    // Either of them. Don't leak shaders.
    glDeleteShader(vertexShader);

    HZ_CORE_ERROR("{0}", infoLog.data());
    HZ_CORE_ASSERT(false, "Fragment shader compilation failure!");
    return;
  }

  // Vertex and fragment shaders are successfully compiled.
  // Now time to link them together into a program.
  // Get a program object.
  id_                  = glCreateProgram();
  unsigned int program = id_;

  // Attach our shaders to our program
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);

  // Link our program
  glLinkProgram(program);

  // Note the different functions here: glGetProgram* instead of glGetShader*.
  int isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
  if (isLinked == GL_FALSE) {
    int maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    std::vector<char> infoLog(maxLength);
    glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

    // We don't need the program anymore.
    glDeleteProgram(program);
    // Don't leak shaders either.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    HZ_CORE_ERROR("{0}", infoLog.data());
    HZ_CORE_ASSERT(false, "Shader link failure!");
    return;
  }

  // Always detach shaders after a successful link.
  glDetachShader(program, vertexShader);
  glDetachShader(program, fragmentShader);
}

OpenGLShader::~OpenGLShader() { glDeleteProgram(id_); }

std::string OpenGLShader::ReadFile(const std::string& filepath) {
  std::string   result;
  std::ifstream in(filepath, std::ios::in | std::ios::binary);
  if (in) {
    in.seekg(0, std::ios::end);
    size_t size = in.tellg();
    if (size != -1) {
      result.resize(size);
      in.seekg(0, std::ios::beg);
      in.read(&result[0], size);
      in.close();
    } else {
      HZ_CORE_ERROR("Could not read from file '{0}'", filepath);
    }
  } else {
    HZ_CORE_ERROR("Could not open file '{0}'", filepath);
  }
  return result;
}

std::unordered_map<unsigned int, std::string> OpenGLShader::PreProcess(
    const std::string& source) {
  std::unordered_map<unsigned int, std::string> shaderSources;

  const char* typeToken       = "#type";
  size_t      typeTokenLength = strlen(typeToken);
  size_t      pos             = source.find(typeToken, 0);
  while (pos != std::string::npos) {
    size_t eol = source.find_first_of('\n', pos);
    HZ_CORE_ASSERT(eol != std::string::npos, "Syntax error");
    size_t      begin = pos + typeTokenLength + 1;
    std::string type  = source.substr(begin, eol - begin);
    HZ_CORE_ASSERT(ShaderTypeFromString(type), "Invalid shader type specified");

    size_t nextLinePos = source.find_first_not_of('\n', eol);
    HZ_CORE_ASSERT(nextLinePos != std::string::npos, "syntax error");
    pos = source.find(typeToken, nextLinePos);

    shaderSources[ShaderTypeFromString(type)] = source.substr(
        nextLinePos, pos - (nextLinePos == std::string::npos ? source.size() - 1
                                                             : nextLinePos));
  }

  return shaderSources;
}

void OpenGLShader::Compile(
    const std::unordered_map<unsigned int, std::string>& shaderSources) {
  GLuint program = glCreateProgram();
  HZ_CORE_ASSERT(shaderSources.size() <= 2,
                 "We only support 2 shaders for now");
  std::array<unsigned int, 2> glShaderIDs{};
  int                         glShaderIDIndex = 0;
  for (auto& kv : shaderSources) {
    unsigned int       type   = kv.first;
    const std::string& source = kv.second;

    GLuint shader = glCreateShader(type);

    const char* sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, 0);

    glCompileShader(shader);

    int isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
      int maxLength = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

      std::vector<char> infoLog(maxLength);
      glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

      glDeleteShader(shader);

      HZ_CORE_ERROR("{0}", infoLog.data());
      HZ_CORE_ASSERT(false, "Shader compilation failure!");
      break;
    }

    glAttachShader(program, shader);
    glShaderIDs[glShaderIDIndex++] = shader;
  }

  id_ = program;

  // Link our program
  glLinkProgram(program);

  // Note the different functions here: glGetProgram* instead of glGetShader*.
  int isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
  if (isLinked == GL_FALSE) {
    int maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    std::vector<char> infoLog(maxLength);
    glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

    // We don't need the program anymore.
    glDeleteProgram(program);

    for (auto id : glShaderIDs) glDeleteShader(id);

    HZ_CORE_ERROR("{0}", infoLog.data());
    HZ_CORE_ASSERT(false, "Shader link failure!");
    return;
  }

  for (auto id : glShaderIDs) {
    glDetachShader(program, id);
    glDeleteShader(id);
  }
}

void OpenGLShader::Bind() const { glUseProgram(id_); }

void OpenGLShader::UnBind() const { glUseProgram(0); }

void OpenGLShader::SetInt(const std::string& name, int value) {
  UploadUniformInt(name, value);
}

void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value) {
  UploadUniformFloat3(name, value);
}

void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value) {
  UploadUniformFloat4(name, value);
}

void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value) {
  UploadUniformMat4(name, value);
}

void OpenGLShader::UploadUniformInt(const std::string& name, int value) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniform1i(location, value);
}

void OpenGLShader::UploadUniformFloat(const std::string& name,
                                      float              value) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniform1f(location, value);
}

void OpenGLShader::UploadUniformFloat2(const std::string& name,
                                       const glm::vec2&   value) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniform2f(location, value.x, value.y);
}

void OpenGLShader::UploadUniformFloat3(const std::string& name,
                                       const glm::vec3&   value) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniform3f(location, value.x, value.y, value.z);
}

void OpenGLShader::UploadUniformFloat4(const std::string& name,
                                       const glm::vec4&   value) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniform4f(location, value.x, value.y, value.z, value.w);
}

void OpenGLShader::UploadUniformMat3(const std::string& name,
                                     const glm::mat3&   matrix) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
}

void OpenGLShader::UploadUniformMat4(const std::string& name,
                                     const glm::mat4&   matrix) const {
  int location = glGetUniformLocation(id_, name.c_str());
  glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
}

}  // namespace hazel
