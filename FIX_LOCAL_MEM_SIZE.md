# Исправление local_mem_size = 0 в KFD

## Root Cause (найдена!)

Файл: `drivers/gpu/drm/amd/amdkfd/kfd_topology.c`, строка **525**

```c
sysfs_show_64bit_prop(buffer, offs, "local_mem_size", 0ULL);
```

**local_mem_size жестко закодирован как 0!**

Это происходит в функции `kfd_sy sfs_node_show()` которая заполняет properties для sysfs.

## Где должно быть реальное значение

Данные о local_mem доступны через:
1. `amdgpu_amdkfd_get_local_mem_info()` - возвращает `local_mem_size_public` и `local_mem_size_private`
2. Вызывается в `kfd_generate_gpu_id()` (строка ~1109) и `kfd_fill_mem_clk_max_info()` (строка ~1192)
3. Но результат **НЕ СОХРАНЯЕТСЯ** в структуру `kfd_topology_device`!

## Патч для исправления

### Вариант 1: Простой патч (рекомендуется)

Изменить строку 525 в `kfd_topology.c`:

```diff
- sysfs_show_64bit_prop(buffer, offs, "local_mem_size", 0ULL);
+ uint64_t local_mem = 0;
+ if (dev->gpu) {
+     struct kfd_local_mem_info mem_info;
+     amdgpu_amdkfd_get_local_mem_info(dev->gpu->kgd, &mem_info);
+     local_mem = mem_info.local_mem_size_public + mem_info.local_mem_size_private;
+ }
+ sysfs_show_64bit_prop(buffer, offs, "local_mem_size", local_mem);
```

### Вариант 2: Правильный патч (добавить в структуру)

1. **Добавить поле в структуру** `kfd_topology_device` (в `kfd_priv.h`):

```c
struct kfd_topology_device {
    // ...
    uint64_t local_mem_size;  // ← Добавить это поле
    // ...
};
```

2. **Заполнить при инициализации** в `kfd_topology_add_device()` (после строки 1392):

```c
// После строки: kfd_fill_mem_clk_max_info(dev);
struct kfd_local_mem_info local_mem_info;
amdgpu_amdkfd_get_local_mem_info(dev->gpu->kgd, &local_mem_info);
dev->local_mem_size = local_mem_info.local_mem_size_public + 
                     local_mem_info.local_mem_size_private;
```

3. **Использовать** в `kfd_sysfs_node_show()` (строка 525):

```c
sysfs_show_64bit_prop(buffer, offs, "local_mem_size", dev->local_mem_size);
```

## Применение патча

### Создать файл патча

```bash
cd /home/noob404/Documents/myPS4Linux
cat > kfd_local_mem_fix.patch << 'EOF'
--- a/drivers/gpu/drm/amd/amdkfd/kfd_topology.c
+++ b/drivers/gpu/drm/amd/amdkfd/kfd_topology.c
@@ -522,7 +522,12 @@ static ssize_t node_show(struct kobject *kobj, struct kobj_attribute *attr,
 		sysfs_show_32bit_prop(buffer, offs, "max_engine_clk_fcompute",
 				dev->node_props.max_engine_clk_fcompute);
 
-		sysfs_show_64bit_prop(buffer, offs, "local_mem_size", 0ULL);
+		uint64_t local_mem = 0;
+		if (dev->gpu) {
+			struct kfd_local_mem_info mem_info;
+			amdgpu_amdkfd_get_local_mem_info(dev->gpu->kgd, &mem_info);
+			local_mem = mem_info.local_mem_size_public + mem_info.local_mem_size_private;
+		}
+		sysfs_show_64bit_prop(buffer, offs, "local_mem_size", local_mem);
 
 		sysfs_show_32bit_prop(buffer, offs, "fw_version",
 			      dev->gpu->mec_fw_version);
EOF
```

### Применить патч

```bash
# Проверить что патч корректен
patch --dry-run -p1 < kfd_local_mem_fix.patch

# Применить
patch -p1 < kfd_local_mem_fix.patch
```

### Пересобрать модуль

```bash
# Пересобрать только amdkfd
make -C /lib/modules/$(uname -r)/build M=$PWD/drivers/gpu/drm/amd/amdkfd modules

# Или пересобрать весь amdgpu
make -C /lib/modules/$(uname -r)/build M=$PWD/drivers/gpu/drm/amd/amdgpu modules
```

### Установить и перезагрузить модуль

```bash
# Выгрузить старый модуль
sudo rmmod amdgpu

# Установить новый
sudo cp drivers/gpu/drm/amd/amdgpu/amdgpu.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/amd/amdgpu/
sudo modprobe amdgpu

# Проверить результат
cat /sys/class/kfd/kfd/topology/nodes/1/properties | grep local_mem_size
```

Ожидаемый результат:
```
local_mem_size 1073741824
```
(1073741824 bytes = 1024 MB)

## Альтернатива: Прямое редактирование

Вместо патча можно напрямую отредактировать файл:

```bash
nano drivers/gpu/drm/amd/amdkfd/kfd_topology.c
```

Найти строку 525 и изменить согласно патчу выше.
