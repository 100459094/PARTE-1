#!/bin/bash

# Colores para la salida
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuración
SERVER_PORT=3030
SERVER_BIN="./servidor"
CLIENT_BIN="python3 cliente.py"
TEST_FILE="/tmp/test_file.txt"
DOWNLOAD_FILE="/tmp/downloaded_file.txt"

# Función para imprimir mensajes
log() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Función para esperar a que el servidor esté listo
wait_for_server() {
    sleep 2
}

# Función para matar procesos
cleanup() {
    log "Limpiando procesos..."
    pkill -f "servidor"
    rm -f $TEST_FILE $DOWNLOAD_FILE
    exit 1
}

# Capturar Ctrl+C
trap cleanup SIGINT

# Verificar que los ejecutables existen
if [ ! -f "$SERVER_BIN" ]; then
    error "Servidor no encontrado. Compilando..."
    gcc servidor.c -o servidor -pthread
    if [ $? -ne 0 ]; then
        error "Error compilando el servidor"
        exit 1
    fi
fi

# Crear archivo de prueba
echo "Este es un archivo de prueba" > $TEST_FILE

# Iniciar el servidor
log "Iniciando servidor en puerto $SERVER_PORT..."
$SERVER_BIN -p $SERVER_PORT &
SERVER_PID=$!
wait_for_server

# Función para ejecutar comandos del cliente
run_client() {
    local cmd="$1"
    local expected="$2"
    local description="$3"
    ((TOTAL_TESTS++))
    
    result=$(echo "$cmd" | $CLIENT_BIN -s localhost -p $SERVER_PORT 2>&1)
    
    if echo "$result" | grep -q "$expected"; then
        success "$description - OK"
        return 0
    else
        error "$description - FAILED"
        error "Esperado: $expected"
        error "Obtenido: $result"
        return 1
    fi
}

# Tests
log "Iniciando pruebas..."

# Test 1: Registro de usuarios
log "Test 1: Registro de usuarios"
run_client "REGISTER usuario1" "REGISTER OK" "Registro usuario1"
run_client "REGISTER usuario2" "REGISTER OK" "Registro usuario2"
run_client "REGISTER usuario1" "USERNAME IN USE" "Registro duplicado"

# Test 2: Conexión
log "Test 2: Conexión de usuarios"
run_client "CONNECT usuario1" "CONNECT OK" "Conexión usuario1"
run_client "CONNECT usuario2" "CONNECT OK" "Conexión usuario2"
run_client "CONNECT usuario1" "USER ALREADY CONNECTED" "Conexión duplicada"

# Test 3: Publicación de archivos
log "Test 3: Publicación de archivos"
run_client "PUBLISH $TEST_FILE 'Archivo de prueba'" "PUBLISH OK" "Publicar archivo"
run_client "PUBLISH $TEST_FILE 'Archivo de prueba'" "PUBLISH FAIL, CONTENT ALREADY PUBLISHED" "Publicar duplicado"

# Test 4: Listar usuarios
log "Test 4: Listar usuarios"
run_client "LIST USERS" "LIST_USERS OK" "Listar usuarios"

# Test 5: Listar contenido
log "Test 5: Listar contenido"
run_client "LIST CONTENT usuario1" "LIST_CONTENT OK" "Listar contenido"

# Test 6: Transferencia de archivos
log "Test 6: Transferencia de archivos"
run_client "GET_FILE usuario1 $TEST_FILE $DOWNLOAD_FILE" "GET_FILE OK" "Transferencia de archivo"

# Test 7: Borrado de archivos
log "Test 7: Borrado de archivos"
run_client "DELETE $TEST_FILE" "DELETE OK" "Borrar archivo"
run_client "DELETE $TEST_FILE" "DELETE FAIL, CONTENT NOT PUBLISHED" "Borrar archivo inexistente"

# Test 8: Desconexión
log "Test 8: Desconexión de usuarios"
run_client "DISCONNECT usuario1" "DISCONNECT OK" "Desconexión usuario1"
run_client "DISCONNECT usuario2" "DISCONNECT OK" "Desconexión usuario2"

# Test 9: Dar de baja
log "Test 9: Dar de baja usuarios"
run_client "UNREGISTER usuario1" "UNREGISTER OK" "Baja usuario1"
run_client "UNREGISTER usuario2" "UNREGISTER OK" "Baja usuario2"

# Test 10: Pruebas de error
log "Test 10: Pruebas de error"
run_client "CONNECT usuario_inexistente" "CONNECT FAIL" "Conexión usuario inexistente"
run_client "LIST CONTENT usuario_inexistente" "LIST_CONTENT FAIL" "Listar contenido usuario inexistente"

# Limpieza final
log "Finalizando pruebas..."
cleanup

# Resumen
echo
log "Resumen de pruebas:"
echo "Tests ejecutados: $TOTAL_TESTS"
echo "Tests exitosos: $PASSED_TESTS"
echo "Tests fallidos: $((TOTAL_TESTS - PASSED_TESTS))"

if [ $((TOTAL_TESTS - PASSED_TESTS)) -eq 0 ]; then
    success "Todas las pruebas pasaron correctamente"
else
    error "Algunas pruebas fallaron"
fi